#include "webrtc_session.h"

#include <utility>

namespace live_stream {
namespace webrtc_internal {
namespace {

bool IsSupportedCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

std::string LocalCandidateIp(const WebrtcOptions &options) {
    if (!options.public_ip.empty() && options.public_ip != "0.0.0.0") {
        return options.public_ip;
    }
    return std::string();
}

std::string BuildLocalIceUfrag(const std::string &peer_id) {
    return "ls" + std::to_string(BuildWebrtcSsrc(peer_id + ":ufrag"));
}

std::string BuildLocalIcePassword(const std::string &peer_id) {
    return "live_stream_" +
           std::to_string(BuildWebrtcSsrc(peer_id + ":pwd")) +
           std::to_string(BuildWebrtcSsrc(peer_id + ":ice"));
}

WebrtcSdpAnswerOptions BuildAnswerOptions(
    const WebrtcPeerInfo &peer,
    const WebrtcOptions &options,
    const DtlsFingerprint &local_fingerprint,
    const std::string &local_ice_ufrag,
    const std::string &local_ice_pwd,
    const NetAddress &local_candidate) {
    WebrtcSdpAnswerOptions answer_options;
    answer_options.peer_id = peer.peer_id;
    answer_options.local_codec = peer.codec;
    answer_options.local_ip = LocalCandidateIp(options);
    answer_options.local_candidate_ip = local_candidate.ip;
    answer_options.local_port = local_candidate.port;
    answer_options.local_ice_ufrag = local_ice_ufrag;
    answer_options.local_ice_pwd = local_ice_pwd;
    answer_options.local_fingerprint_hash =
        DtlsFingerprintAlgorithmName(local_fingerprint.algorithm);
    answer_options.local_fingerprint = local_fingerprint.value;
    answer_options.local_ssrc = BuildWebrtcSsrc(peer.peer_id);
    return answer_options;
}

}  // namespace

WebrtcSession::WebrtcSession(WebrtcPeerInfo peer)
    : peer_(std::move(peer)) {}

WebrtcSession::~WebrtcSession() {
    Close();
}

bool WebrtcSession::HandleOffer(const std::string &offer_sdp,
                                const WebrtcSessionOfferContext &context,
                                WebrtcSessionOfferResult *result) {
    if (result == nullptr || peer_.peer_id.empty() ||
        !IsSupportedCodec(peer_.codec) || offer_sdp.empty()) {
        return false;
    }

    WebrtcSdpOffer parsed_offer;
    if (!ParseWebrtcOffer(offer_sdp, peer_.codec, &parsed_offer)) {
        return false;
    }
    DtlsFingerprint remote_fingerprint;
    if (!ParseRemoteFingerprint(offer_sdp, &remote_fingerprint)) {
        return false;
    }

    const std::string local_ice_ufrag = BuildLocalIceUfrag(peer_.peer_id);
    const std::string local_ice_pwd = BuildLocalIcePassword(peer_.peer_id);

    WebrtcTransportStartOptions transport_options;
    transport_options.net_engine = context.net_engine;
    transport_options.udp_callbacks = context.udp_callbacks;
    transport_options.peer_id = peer_.peer_id;
    transport_options.local_port_base = context.options.local_port_base;
    transport_options.port_count =
        context.options.max_peers == 0 ? 1U : context.options.max_peers;
    transport_options.next_port_offset = context.next_port_offset;
    transport_options.local_ice_ufrag = local_ice_ufrag;
    transport_options.local_ice_password = local_ice_pwd;
    transport_options.remote_fingerprint = remote_fingerprint;
    transport_options.timer_user = context.timer_user;
    transport_options.on_dtls_timeout = context.on_dtls_timeout;

    std::unique_ptr<WebrtcTransport> transport(new WebrtcTransport());
    NetAddress local_candidate;
    uint32_t next_port_offset = context.next_port_offset;
    if (!transport->Start(transport_options, &next_port_offset,
                          &local_candidate)) {
        return false;
    }

    const WebrtcSdpAnswerOptions answer_options = BuildAnswerOptions(
        peer_, context.options, context.local_fingerprint, local_ice_ufrag,
        local_ice_pwd, local_candidate);
    const std::string answer = BuildWebrtcAnswer(parsed_offer, answer_options);
    if (answer.empty()) {
        return false;
    }

    offer_sdp_ = offer_sdp;
    answer_sdp_ = answer;
    offer_ = parsed_offer;
    remote_fingerprint_ = remote_fingerprint;
    local_ice_ufrag_ = local_ice_ufrag;
    local_ice_pwd_ = local_ice_pwd;
    transport_ = std::move(transport);
    rtp_payload_type_ =
        static_cast<uint8_t>(parsed_offer.video_codec.payload_type);
    rtp_clock_rate_ = parsed_offer.video_codec.clock_rate;
    rtp_ssrc_ = answer_options.local_ssrc;

    result->answer_sdp = answer;
    result->next_port_offset = next_port_offset;
    return true;
}

bool WebrtcSession::AddIceCandidate(
    const WebrtcIceCandidate &candidate) {
    if (candidate.peer_id != peer_.peer_id || candidate.candidate.empty()) {
        return false;
    }
    has_remote_candidate_ = true;
    return true;
}

void WebrtcSession::Close() {
    if (transport_ != nullptr) {
        transport_->Close();
    }
    transport_.reset();
    has_remote_candidate_ = false;
}

bool WebrtcSession::MatchesSocket(UdpSocketId socket_id) const {
    return transport_ != nullptr && transport_->MatchesSocket(socket_id);
}

bool WebrtcSession::HandleIcePacket(NetAddress peer, const uint8_t *data,
                                    size_t size, bool *connected_now) {
    return transport_ != nullptr &&
           transport_->HandleIcePacket(std::move(peer), data, size,
                                       connected_now);
}

bool WebrtcSession::ProcessDtlsPacket(
    const uint8_t *data, size_t size, WebrtcTransportDtlsResult *result) {
    return transport_ != nullptr &&
           transport_->ProcessDtlsPacket(data, size, result);
}

bool WebrtcSession::HandleDtlsTimeout(
    WebrtcTransportDtlsResult *result) {
    return transport_ != nullptr && transport_->HandleDtlsTimeout(result);
}

bool WebrtcSession::SendDtlsResult(
    const WebrtcTransportDtlsResult &result) {
    return transport_ != nullptr && transport_->SendDtlsResult(result);
}

bool WebrtcSession::HandleSrtcpPacket(const uint8_t *data, size_t size,
                                      bool *request_key_frame) {
    return transport_ != nullptr &&
           transport_->HandleSrtcpPacket(data, size, request_key_frame);
}

bool WebrtcSession::SendRtpPacket(
    const EncodedFrame &frame, const rtp::RtpPacketView &packet) {
    return transport_ != nullptr && transport_->SendRtpPacket(frame, packet);
}

bool WebrtcSession::GetRtpSendParameters(
    WebrtcRtpSendParameters *parameters) const {
    if (parameters == nullptr || rtp_payload_type_ == 0 || rtp_ssrc_ == 0) {
        return false;
    }
    parameters->codec = peer_.codec;
    parameters->payload_type = rtp_payload_type_;
    parameters->clock_rate = rtp_clock_rate_;
    parameters->ssrc = rtp_ssrc_;
    return true;
}

void WebrtcSession::FillPeerDiagnostics(WebrtcPeerInfo *peer) const {
    if (peer == nullptr || transport_ == nullptr) {
        return;
    }
    const WebrtcTransportDiagnostics diagnostics =
        transport_->GetDiagnostics();
    peer->ice_selected = diagnostics.ice_selected;
    peer->dtls_state = diagnostics.dtls_state;
    peer->srtp_ready = diagnostics.srtp_ready;
    peer->rtp_packets = diagnostics.rtp_packets;
    peer->rtp_bytes = diagnostics.rtp_bytes;
    peer->rtcp_packets = diagnostics.rtcp_packets;
    peer->rtcp_bytes = diagnostics.rtcp_bytes;
    peer->rtcp_pli_count = diagnostics.rtcp_pli_count;
    peer->rtcp_fir_count = diagnostics.rtcp_fir_count;
    peer->rtcp_nack_count = diagnostics.rtcp_nack_count;
    peer->rtcp_transport_cc_count = diagnostics.rtcp_transport_cc_count;
    peer->rtcp_keyframe_requests = diagnostics.rtcp_keyframe_requests;
}

void WebrtcSession::FillStats(WebrtcStats *stats) const {
    if (transport_ != nullptr) {
        transport_->FillStats(stats);
    }
}

bool WebrtcSession::ice_connected() const {
    return transport_ != nullptr && transport_->ice_connected();
}

}  // namespace webrtc_internal
}  // namespace live_stream
