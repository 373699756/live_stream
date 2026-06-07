#include "webrtc_transport.h"

#include "stun_packet.h"

#include <utility>

namespace live_stream {
namespace webrtc_internal {
namespace {

uint16_t PortWithOffset(uint16_t base_port, uint32_t offset) {
    const uint32_t port = static_cast<uint32_t>(base_port) + offset;
    if (port == 0 || port > 65535U) {
        return 0;
    }
    return static_cast<uint16_t>(port);
}

SrtpCryptoSuite ToSrtpCryptoSuite(DtlsSrtpCryptoSuite suite) {
    switch (suite) {
        case DtlsSrtpCryptoSuite::kAes128CmSha1_80:
            return SrtpCryptoSuite::kAes128CmSha180;
        case DtlsSrtpCryptoSuite::kNone:
            return SrtpCryptoSuite::kNone;
    }
    return SrtpCryptoSuite::kNone;
}

const char *DtlsStateName(DtlsState state) {
    switch (state) {
        case DtlsState::kNew:
            return "new";
        case DtlsState::kConnecting:
            return "connecting";
        case DtlsState::kConnected:
            return "connected";
        case DtlsState::kFailed:
            return "failed";
        case DtlsState::kClosed:
            return "closed";
    }
    return "unknown";
}

}  // namespace

WebrtcTransport::~WebrtcTransport() {
    Close();
}

bool WebrtcTransport::Start(const WebrtcTransportStartOptions &options,
                            uint32_t *next_port_offset,
                            NetAddress *local_candidate) {
    if (next_port_offset == nullptr || local_candidate == nullptr ||
        options.net_engine == nullptr || options.peer_id.empty() ||
        options.local_ice_ufrag.empty() ||
        options.local_ice_password.empty()) {
        return false;
    }

    Close();

    std::unique_ptr<DtlsTransport> dtls(new DtlsTransport());
    if (!dtls->StartServer(options.remote_fingerprint)) {
        return false;
    }

    std::unique_ptr<IceTransport> ice;
    uint32_t updated_port_offset = options.next_port_offset;
    if (!StartIceTransport(options, &updated_port_offset, &ice)) {
        dtls->Close();
        return false;
    }

    peer_id_ = options.peer_id;
    net_engine_ = options.net_engine;
    timer_user_ = options.timer_user;
    on_dtls_timeout_ = options.on_dtls_timeout;
    ice_ = std::move(ice);
    dtls_ = std::move(dtls);
    *next_port_offset = updated_port_offset;
    *local_candidate = ice_->local_address();
    return true;
}

void WebrtcTransport::Close() {
    CancelDtlsTimer();
    outbound_srtp_.Close();
    inbound_srtp_.Close();
    if (dtls_ != nullptr) {
        dtls_->Close();
    }
    if (ice_ != nullptr) {
        ice_->Stop();
    }
    dtls_.reset();
    ice_.reset();
    peer_id_.clear();
    net_engine_ = nullptr;
    timer_user_ = nullptr;
    on_dtls_timeout_ = nullptr;
    protected_rtp_packets_ = 0;
    protected_rtp_bytes_ = 0;
    ice_connected_ = false;
    dtls_connected_ = false;
}

bool WebrtcTransport::HandleIcePacket(NetAddress peer, const uint8_t *data,
                                      size_t size, bool *connected_now) {
    if (connected_now != nullptr) {
        *connected_now = false;
    }
    if (ice_ == nullptr) {
        return false;
    }
    bool selected_now = false;
    if (!ice_->HandleUdpPacket(std::move(peer), data, size, &selected_now)) {
        return false;
    }
    if (selected_now) {
        ice_connected_ = true;
        if (connected_now != nullptr) {
            *connected_now = true;
        }
    }
    return true;
}

bool WebrtcTransport::ProcessDtlsPacket(
    const uint8_t *data, size_t size, WebrtcTransportDtlsResult *result) {
    if (result == nullptr) {
        return false;
    }
    *result = WebrtcTransportDtlsResult();
    if (dtls_ == nullptr || data == nullptr || size == 0) {
        result->failed = true;
        return false;
    }

    DtlsProcessResult dtls_result;
    if (!dtls_->ProcessPacket(data, size, &dtls_result)) {
        result->failed = true;
        result->error = dtls_result.error;
        return false;
    }
    return ApplyDtlsResult(dtls_result, result);
}

bool WebrtcTransport::HandleDtlsTimeout(
    WebrtcTransportDtlsResult *result) {
    if (result == nullptr) {
        return false;
    }
    *result = WebrtcTransportDtlsResult();
    if (dtls_ == nullptr) {
        return false;
    }
    dtls_timer_id_ = 0;

    DtlsProcessResult dtls_result;
    if (!dtls_->HandleTimeout(&dtls_result)) {
        result->failed = true;
        result->error = dtls_result.error;
        return false;
    }
    return ApplyDtlsResult(dtls_result, result);
}

bool WebrtcTransport::SendDtlsResult(
    const WebrtcTransportDtlsResult &result) {
    if (result.outgoing_dtls.empty()) {
        return true;
    }
    return ice_ != nullptr && ice_->connected() &&
           ice_->SendToSelected(result.outgoing_dtls.data(),
                                result.outgoing_dtls.size());
}

bool WebrtcTransport::HandleSrtcpPacket(const uint8_t *data, size_t size,
                                        bool *request_key_frame) {
    if (request_key_frame == nullptr || !inbound_srtp_.ready() ||
        data == nullptr || size == 0) {
        return false;
    }
    std::vector<uint8_t> plain_rtcp;
    if (!inbound_srtp_.UnprotectRtcp(data, size, &plain_rtcp)) {
        return false;
    }
    RtcpFeedback feedback;
    if (!SrtpSession::ParseRtcpFeedback(plain_rtcp.data(),
                                        plain_rtcp.size(), &feedback)) {
        *request_key_frame = false;
        return true;
    }
    *request_key_frame = SrtpSession::IsKeyFrameRequest(feedback.type);
    return true;
}

bool WebrtcTransport::SendRtpPacket(
    const EncodedFrame &frame, const media_mux::RtpPacketView &packet) {
    if (ice_ == nullptr || !ice_->connected() || !outbound_srtp_.ready() ||
        !EncodedFrameHasPayload(&frame) || packet.Size() == 0 ||
        packet.ssrc == 0 || packet.payload_type == 0) {
        return false;
    }
    std::vector<uint8_t> protected_packet;
    if (!outbound_srtp_.ProtectRtp(packet, &protected_packet) ||
        protected_packet.empty() ||
        !ice_->SendToSelected(protected_packet.data(),
                              protected_packet.size())) {
        return false;
    }
    ++protected_rtp_packets_;
    protected_rtp_bytes_ += protected_packet.size();
    return true;
}

bool WebrtcTransport::MatchesSocket(UdpSocketId socket_id) const {
    return socket_id != 0 && ice_ != nullptr && ice_->socket_id() == socket_id;
}

bool WebrtcTransport::ice_connected() const {
    return ice_ != nullptr && ice_->connected();
}

bool WebrtcTransport::srtp_ready() const {
    return outbound_srtp_.ready() && inbound_srtp_.ready();
}

UdpSocketId WebrtcTransport::socket_id() const {
    return ice_ == nullptr ? 0 : ice_->socket_id();
}

NetAddress WebrtcTransport::local_address() const {
    return ice_ == nullptr ? NetAddress() : ice_->local_address();
}

WebrtcTransportDiagnostics WebrtcTransport::GetDiagnostics() const {
    WebrtcTransportDiagnostics diagnostics;
    diagnostics.ice_selected = ice_ != nullptr && ice_->connected();
    diagnostics.dtls_state =
        dtls_ == nullptr ? "closed" : DtlsStateName(dtls_->state());
    diagnostics.srtp_ready = srtp_ready();
    diagnostics.rtp_packets = protected_rtp_packets_;
    diagnostics.rtp_bytes = protected_rtp_bytes_;
    return diagnostics;
}

void WebrtcTransport::FillStats(WebrtcStats *stats) const {
    if (stats == nullptr) {
        return;
    }
    if (ice_ != nullptr && ice_->connected()) {
        ++stats->selected_ice_pairs;
    }
}

bool WebrtcTransport::IsIcePacket(const uint8_t *data, size_t size) {
    return IsStunPacket(data, size);
}

bool WebrtcTransport::IsDtlsPacket(const uint8_t *data, size_t size) {
    return DtlsTransport::IsDtlsPacket(data, size);
}

bool WebrtcTransport::IsRtcpPacket(const uint8_t *data, size_t size) {
    return data != nullptr && size >= 2 && data[0] >= 128 &&
           data[0] <= 191 && data[1] >= 192 && data[1] <= 223;
}

bool WebrtcTransport::StartIceTransport(
    const WebrtcTransportStartOptions &options,
    uint32_t *next_port_offset,
    std::unique_ptr<IceTransport> *ice) {
    if (next_port_offset == nullptr || ice == nullptr ||
        options.net_engine == nullptr) {
        return false;
    }

    const uint32_t port_count = options.port_count == 0 ? 1U :
        options.port_count;
    for (uint32_t i = 0; i < port_count; ++i) {
        const uint32_t offset =
            (options.next_port_offset + i) % port_count;
        const uint16_t port = PortWithOffset(options.local_port_base, offset);
        if (port == 0) {
            continue;
        }
        std::unique_ptr<IceTransport> candidate(
            new IceTransport(options.peer_id));
        if (!candidate->Start(options.net_engine, options.udp_callbacks,
                              "0.0.0.0", port, options.local_ice_ufrag,
                              options.local_ice_password)) {
            continue;
        }
        const NetAddress local_address = candidate->local_address();
        if (local_address.port == 0) {
            candidate->Stop();
            continue;
        }
        *next_port_offset = (offset + 1) % port_count;
        *ice = std::move(candidate);
        return true;
    }
    return false;
}

bool WebrtcTransport::ApplyDtlsResult(
    const DtlsProcessResult &dtls_result,
    WebrtcTransportDtlsResult *result) {
    if (result == nullptr) {
        return false;
    }
    result->outgoing_dtls = dtls_result.outgoing_dtls;
    if (dtls_result.state == DtlsState::kConnected && !dtls_connected_) {
        if (!StartSrtp(dtls_result.srtp_keys)) {
            result->failed = true;
            result->error = "srtp_start_failed";
            return false;
        }
        dtls_connected_ = true;
        CancelDtlsTimer();
        result->connected_now = true;
    } else if (dtls_result.state == DtlsState::kConnecting) {
        if (!ArmDtlsTimer()) {
            result->failed = true;
            result->error = "dtls_timer_failed";
            return false;
        }
    } else if (dtls_result.state == DtlsState::kFailed) {
        result->failed = true;
        result->error = dtls_result.error;
        return false;
    }
    return true;
}

bool WebrtcTransport::StartSrtp(const DtlsSrtpKeys &keys) {
    const SrtpCryptoSuite suite = ToSrtpCryptoSuite(keys.suite);
    if (suite == SrtpCryptoSuite::kNone ||
        keys.local_master_key.empty() ||
        keys.remote_master_key.empty()) {
        return false;
    }
    if (!outbound_srtp_.Start(SrtpDirection::kOutbound, suite,
                              keys.local_master_key)) {
        return false;
    }
    if (!inbound_srtp_.Start(SrtpDirection::kInbound, suite,
                             keys.remote_master_key)) {
        outbound_srtp_.Close();
        return false;
    }
    return true;
}

bool WebrtcTransport::ArmDtlsTimer() {
    if (net_engine_ == nullptr || dtls_ == nullptr ||
        dtls_->state() != DtlsState::kConnecting ||
        on_dtls_timeout_ == nullptr) {
        return true;
    }
    uint32_t timeout_ms = 0;
    if (!dtls_->GetHandshakeTimeoutMs(&timeout_ms)) {
        return true;
    }
    CancelDtlsTimer();

    void *timer_user = timer_user_;
    WebrtcTransportTimerFn on_dtls_timeout = on_dtls_timeout_;
    const std::string peer_id = peer_id_;
    const NetTimerId timer_id = net_engine_->RunOnIoAfter(
        timeout_ms, [timer_user, on_dtls_timeout, peer_id]() {
            on_dtls_timeout(timer_user, peer_id);
        });
    if (timer_id == 0) {
        return false;
    }
    dtls_timer_id_ = timer_id;
    return true;
}

void WebrtcTransport::CancelDtlsTimer() {
    if (dtls_timer_id_ == 0) {
        return;
    }
    if (net_engine_ != nullptr) {
        (void)net_engine_->CancelIoTimer(dtls_timer_id_);
    }
    dtls_timer_id_ = 0;
}

}  // namespace webrtc_internal
}  // namespace live_stream
