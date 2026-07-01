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
                            uint32_t &next_port_offset,
                            SocketAddress &local_candidate) {
    if (options.socket_io == nullptr || options.socket_loop == nullptr ||
        options.peer_id.empty() || options.local_ice_ufrag.empty() ||
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
    if (!StartIceTransport(options, updated_port_offset, ice)) {
        dtls->Close();
        return false;
    }

    peer_id_ = options.peer_id;
    socket_io_ = options.socket_io;
    net_loop_ = options.socket_loop;
    timer_user_ = options.timer_user;
    on_dtls_timeout_ = options.on_dtls_timeout;
    ice_ = std::move(ice);
    dtls_ = std::move(dtls);
    next_port_offset = updated_port_offset;
    local_candidate = ice_->local_address();
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
    socket_io_ = nullptr;
    net_loop_ = nullptr;
    timer_user_ = nullptr;
    on_dtls_timeout_ = nullptr;
    protected_rtp_packets_ = 0;
    protected_rtp_bytes_ = 0;
    rtcp_packets_ = 0;
    rtcp_bytes_ = 0;
    rtcp_pli_packets_ = 0;
    rtcp_fir_packets_ = 0;
    rtcp_nack_packets_ = 0;
    rtcp_transport_cc_packets_ = 0;
    rtcp_keyframe_requests_ = 0;
    protected_rtp_packet_.clear();
    plain_rtcp_packet_.clear();
    ice_connected_ = false;
    dtls_connected_ = false;
}

bool WebrtcTransport::HandleIcePacket(SocketAddress peer, const uint8_t *data,
                                      size_t size, bool &connected_now) {
    connected_now = false;
    if (ice_ == nullptr) {
        return false;
    }
    bool selected_now = false;
    if (!ice_->HandleUdpPacket(std::move(peer), data, size, selected_now)) {
        return false;
    }
    if (selected_now) {
        ice_connected_ = true;
        connected_now = true;
    }
    return true;
}

bool WebrtcTransport::ProcessDtlsPacket(
    const uint8_t *data, size_t size, WebrtcDtlsOutput &result) {
    result = WebrtcDtlsOutput();
    if (dtls_ == nullptr || data == nullptr || size == 0) {
        result.failed = true;
        return false;
    }

    DtlsProcessOutput dtls_result;
    if (!dtls_->ProcessPacket(data, size, &dtls_result)) {
        result.failed = true;
        result.error = dtls_result.error;
        return false;
    }
    return ApplyDtlsResult(dtls_result, result);
}

bool WebrtcTransport::HandleDtlsTimeout(
    WebrtcDtlsOutput &result) {
    result = WebrtcDtlsOutput();
    if (dtls_ == nullptr) {
        return false;
    }
    dtls_timer_id_ = 0;

    DtlsProcessOutput dtls_result;
    if (!dtls_->HandleTimeout(&dtls_result)) {
        result.failed = true;
        result.error = dtls_result.error;
        return false;
    }
    return ApplyDtlsResult(dtls_result, result);
}

bool WebrtcTransport::SendDtlsResult(
    const WebrtcDtlsOutput &result) {
    if (result.outgoing_dtls.empty()) {
        return true;
    }
    return ice_ != nullptr && ice_->connected() &&
           ice_->SendToSelected(result.outgoing_dtls.data(),
                                result.outgoing_dtls.size());
}

bool WebrtcTransport::HandleSrtcpPacket(const uint8_t *data, size_t size,
                                        bool &need_keyframe) {
    need_keyframe = false;
    if (!inbound_srtp_.ready() || data == nullptr || size == 0) {
        return false;
    }
    if (!inbound_srtp_.UnprotectRtcp(data, size, &plain_rtcp_packet_)) {
        return false;
    }
    ++rtcp_packets_;
    rtcp_bytes_ += plain_rtcp_packet_.size();
    RtcpFeedbackStats feedback_stats;
    if (!SrtpSession::ReadRtcpFeedbackStats(plain_rtcp_packet_.data(),
                                            plain_rtcp_packet_.size(),
                                            &feedback_stats)) {
        return true;
    }
    RecordRtcpFeedback(feedback_stats);
    need_keyframe = feedback_stats.pli_packets != 0 ||
                    feedback_stats.fir_packets != 0;
    return true;
}

bool WebrtcTransport::SendRtpPacket(
    const MediaFrame &frame, const rtp::RtpPacketView &packet) {
    if (ice_ == nullptr || !ice_->connected() || !outbound_srtp_.ready() ||
        !IsMediaFramePayloadValid(frame) || packet.Size() == 0 ||
        packet.ssrc == 0 || packet.payload_type == 0) {
        return false;
    }
    // ProtectRtp 会把 packet view 复制成连续 RTP buffer 并原地加密/追加认证尾部。
    // UDP 发送只使用 protected_rtp_packet_；函数返回后不再持有 frame payload。
    if (!outbound_srtp_.ProtectRtp(packet, &protected_rtp_packet_) ||
        protected_rtp_packet_.empty() ||
        !ice_->SendToSelected(protected_rtp_packet_.data(),
                              protected_rtp_packet_.size())) {
        return false;
    }
    ++protected_rtp_packets_;
    protected_rtp_bytes_ += protected_rtp_packet_.size();
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

SocketAddress WebrtcTransport::local_address() const {
    return ice_ == nullptr ? SocketAddress() : ice_->local_address();
}

WebrtcTransportInfo WebrtcTransport::GetInfo() const {
    WebrtcTransportInfo info;
    info.ice_selected = ice_ != nullptr && ice_->connected();
    info.dtls_state =
        dtls_ == nullptr ? "closed" : DtlsStateName(dtls_->state());
    info.srtp_ready = srtp_ready();
    info.rtp_packets = protected_rtp_packets_;
    info.rtp_bytes = protected_rtp_bytes_;
    info.rtcp_packets = rtcp_packets_;
    info.rtcp_bytes = rtcp_bytes_;
    info.rtcp_pli_packets = rtcp_pli_packets_;
    info.rtcp_fir_packets = rtcp_fir_packets_;
    info.rtcp_nack_packets = rtcp_nack_packets_;
    info.rtcp_transport_cc_packets = rtcp_transport_cc_packets_;
    info.rtcp_keyframe_requests = rtcp_keyframe_requests_;
    return info;
}

void WebrtcTransport::FillStats(WebrtcStats &stats) const {
    if (ice_ != nullptr && ice_->connected()) {
        ++stats.selected_ice_pairs;
    }
    stats.rtcp_packets += rtcp_packets_;
    stats.rtcp_bytes += rtcp_bytes_;
    stats.rtcp_pli_packets += rtcp_pli_packets_;
    stats.rtcp_fir_packets += rtcp_fir_packets_;
    stats.rtcp_nack_packets += rtcp_nack_packets_;
    stats.rtcp_transport_cc_packets += rtcp_transport_cc_packets_;
    stats.rtcp_keyframe_requests += rtcp_keyframe_requests_;
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
    uint32_t &next_port_offset,
    std::unique_ptr<IceTransport> &ice) {
    if (options.socket_io == nullptr || options.socket_loop == nullptr) {
        return false;
    }

    const uint32_t port_span = options.port_span == 0 ? 1U : options.port_span;
    for (uint32_t i = 0; i < port_span; ++i) {
        const uint32_t offset =
            (options.next_port_offset + i) % port_span;
        const uint16_t port = PortWithOffset(options.local_port_base, offset);
        if (port == 0) {
            continue;
        }
        std::unique_ptr<IceTransport> candidate(
            new IceTransport(options.peer_id));
        if (!candidate->Start(options.socket_io, options.socket_loop,
                              options.udp_callbacks,
                              "0.0.0.0", port, options.local_ice_ufrag,
                              options.local_ice_password)) {
            continue;
        }
        const SocketAddress local_address = candidate->local_address();
        if (local_address.port == 0) {
            candidate->Stop();
            continue;
        }
        next_port_offset = (offset + 1) % port_span;
        ice = std::move(candidate);
        return true;
    }
    return false;
}

bool WebrtcTransport::ApplyDtlsResult(
    const DtlsProcessOutput &dtls_result,
    WebrtcDtlsOutput &result) {
    result.outgoing_dtls = dtls_result.outgoing_dtls;
    if (dtls_result.state == DtlsState::kConnected && !dtls_connected_) {
        if (!StartSrtp(dtls_result.srtp_keys)) {
            result.failed = true;
            result.error = "srtp_start_failed";
            return false;
        }
        dtls_connected_ = true;
        CancelDtlsTimer();
        result.connected_now = true;
    } else if (dtls_result.state == DtlsState::kConnecting) {
        if (!StartDtlsTimer()) {
            result.failed = true;
            result.error = "dtls_timer_failed";
            return false;
        }
    } else if (dtls_result.state == DtlsState::kFailed) {
        result.failed = true;
        result.error = dtls_result.error;
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

bool WebrtcTransport::StartDtlsTimer() {
    if (net_loop_ == nullptr || dtls_ == nullptr ||
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
    event::TimerId timer_id = 0;
    const event::EventStatus timer_status = net_loop_->RunAfter(
        timeout_ms, [timer_user, on_dtls_timeout, peer_id]() {
            on_dtls_timeout(timer_user, peer_id);
        },
        &timer_id);
    if (timer_status != event::EventStatus::kOk || timer_id == 0) {
        return false;
    }
    dtls_timer_id_ = timer_id;
    return true;
}

void WebrtcTransport::CancelDtlsTimer() {
    if (dtls_timer_id_ == 0) {
        return;
    }
    if (net_loop_ != nullptr) {
        (void)net_loop_->CancelTimer(dtls_timer_id_);
    }
    dtls_timer_id_ = 0;
}

void WebrtcTransport::RecordRtcpFeedback(
    const RtcpFeedbackStats &feedback_stats) {
    rtcp_pli_packets_ += feedback_stats.pli_packets;
    rtcp_fir_packets_ += feedback_stats.fir_packets;
    rtcp_nack_packets_ += feedback_stats.nack_packets;
    rtcp_transport_cc_packets_ += feedback_stats.transport_cc_packets;
    rtcp_keyframe_requests_ += feedback_stats.pli_packets + feedback_stats.fir_packets;
}

}  // namespace webrtc_internal
}  // namespace live_stream
