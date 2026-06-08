#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_SESSION_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_SESSION_H_

#include "dtls_transport.h"
#include "net.h"
#include "webrtc.h"
#include "webrtc_engine.h"
#include "webrtc_sdp.h"
#include "webrtc_transport.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {
namespace webrtc_internal {

struct WebrtcSessionOfferContext {
    WebrtcOptions options;
    DtlsFingerprint local_fingerprint;
    INetEngine *net_engine = nullptr;
    UdpCallbacks udp_callbacks;
    uint32_t next_port_offset = 0;
    void *timer_user = nullptr;
    WebrtcTransportTimerFn on_dtls_timeout = nullptr;
};

struct WebrtcSessionOfferResult {
    std::string answer_sdp;
    uint32_t next_port_offset = 0;
};

class WebrtcSession {
public:
    explicit WebrtcSession(WebrtcPeerInfo peer);
    ~WebrtcSession();

    WebrtcSession(const WebrtcSession &) = delete;
    WebrtcSession &operator=(const WebrtcSession &) = delete;

    bool HandleOffer(const std::string &offer_sdp,
                     const WebrtcSessionOfferContext &context,
                     WebrtcSessionOfferResult *result);
    bool AddIceCandidate(const WebrtcIceCandidate &candidate);
    void Close();

    bool MatchesSocket(UdpSocketId socket_id) const;
    bool HandleIcePacket(NetAddress peer, const uint8_t *data, size_t size,
                         bool *connected_now);
    bool ProcessDtlsPacket(const uint8_t *data, size_t size,
                           WebrtcTransportDtlsResult *result);
    bool HandleDtlsTimeout(WebrtcTransportDtlsResult *result);
    bool SendDtlsResult(const WebrtcTransportDtlsResult &result);
    bool HandleSrtcpPacket(const uint8_t *data, size_t size,
                           bool *request_key_frame);
    bool SendRtpPacket(const EncodedFrame &frame,
                       const rtp::RtpPacketView &packet);

    bool GetRtpSendParameters(WebrtcRtpSendParameters *parameters) const;
    void FillPeerDiagnostics(WebrtcPeerInfo *peer) const;
    void FillStats(WebrtcStats *stats) const;
    bool ice_connected() const;

    const WebrtcPeerInfo &peer() const { return peer_; }

private:
    WebrtcPeerInfo peer_;
    std::string offer_sdp_;
    std::string answer_sdp_;
    WebrtcSdpOffer offer_;
    DtlsFingerprint remote_fingerprint_;
    std::string local_ice_ufrag_;
    std::string local_ice_pwd_;
    std::unique_ptr<WebrtcTransport> transport_;
    uint8_t rtp_payload_type_ = 0;
    uint32_t rtp_clock_rate_ = rtp::kRtpClockRate;
    uint32_t rtp_ssrc_ = 0;
    bool has_remote_candidate_ = false;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_SESSION_H_
