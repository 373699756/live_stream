#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_TRANSPORT_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_TRANSPORT_H_

#include "dtls_transport.h"
#include "ice_transport.h"
#include "rtp.h"
#include "net.h"
#include "srtp_session.h"
#include "webrtc.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {

using WebrtcTransportTimerFn = void (*)(void *user,
                                        const std::string &peer_id);

struct WebrtcTransportStartOptions {
    INetEngine *net_engine = nullptr;
    event::Loop *net_loop = nullptr;
    UdpCallbacks udp_callbacks;
    std::string peer_id;
    uint16_t local_port_base = 0;
    uint32_t port_count = 1;
    uint32_t next_port_offset = 0;
    std::string local_ice_ufrag;
    std::string local_ice_password;
    DtlsFingerprint remote_fingerprint;
    void *timer_user = nullptr;
    WebrtcTransportTimerFn on_dtls_timeout = nullptr;
};

struct WebrtcTransportDtlsResult {
    std::vector<uint8_t> outgoing_dtls;
    bool connected_now = false;
    bool failed = false;
    std::string error;
};

struct WebrtcTransportInfo {
    bool ice_selected = false;
    std::string dtls_state;
    bool srtp_ready = false;
    uint64_t rtp_packets = 0;
    uint64_t rtp_bytes = 0;
    uint64_t rtcp_packets = 0;
    uint64_t rtcp_bytes = 0;
    uint64_t rtcp_pli_count = 0;
    uint64_t rtcp_fir_count = 0;
    uint64_t rtcp_nack_count = 0;
    uint64_t rtcp_transport_cc_count = 0;
    uint64_t rtcp_keyframe_requests = 0;
};

class WebrtcTransport {
public:
    WebrtcTransport() = default;
    ~WebrtcTransport();

    WebrtcTransport(const WebrtcTransport &) = delete;
    WebrtcTransport &operator=(const WebrtcTransport &) = delete;

    bool Start(const WebrtcTransportStartOptions &options,
               uint32_t *next_port_offset,
               NetAddress *local_candidate);
    void Close();

    bool HandleIcePacket(NetAddress peer, const uint8_t *data, size_t size,
                         bool *connected_now);
    bool ProcessDtlsPacket(const uint8_t *data, size_t size,
                           WebrtcTransportDtlsResult *result);
    bool HandleDtlsTimeout(WebrtcTransportDtlsResult *result);
    bool SendDtlsResult(const WebrtcTransportDtlsResult &result);
    bool HandleSrtcpPacket(const uint8_t *data, size_t size,
                           bool *need_keyframe);
    bool SendRtpPacket(const MediaFrame &frame,
                       const rtp::RtpPacketView &packet);

    bool MatchesSocket(UdpSocketId socket_id) const;
    bool ice_connected() const;
    bool srtp_ready() const;
    UdpSocketId socket_id() const;
    NetAddress local_address() const;
    WebrtcTransportInfo GetInfo() const;
    void FillStats(WebrtcStats *stats) const;

    static bool IsIcePacket(const uint8_t *data, size_t size);
    static bool IsDtlsPacket(const uint8_t *data, size_t size);
    static bool IsRtcpPacket(const uint8_t *data, size_t size);

private:
    bool StartIceTransport(const WebrtcTransportStartOptions &options,
                           uint32_t *next_port_offset,
                           std::unique_ptr<IceTransport> *ice);
    bool ApplyDtlsResult(const DtlsProcessResult &dtls_result,
                         WebrtcTransportDtlsResult *result);
    bool StartSrtp(const DtlsSrtpKeys &keys);
    bool ArmDtlsTimer();
    void CancelDtlsTimer();
    void RecordRtcpFeedback(const RtcpFeedbackCounters &counters);

    std::string peer_id_;
    INetEngine *net_engine_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    void *timer_user_ = nullptr;
    WebrtcTransportTimerFn on_dtls_timeout_ = nullptr;
    std::unique_ptr<IceTransport> ice_;
    std::unique_ptr<DtlsTransport> dtls_;
    SrtpSession outbound_srtp_;
    SrtpSession inbound_srtp_;
    std::vector<uint8_t> protected_rtp_packet_;
    std::vector<uint8_t> plain_rtcp_packet_;
    event::TimerId dtls_timer_id_ = 0;
    uint64_t protected_rtp_packets_ = 0;
    uint64_t protected_rtp_bytes_ = 0;
    uint64_t rtcp_packets_ = 0;
    uint64_t rtcp_bytes_ = 0;
    uint64_t rtcp_pli_count_ = 0;
    uint64_t rtcp_fir_count_ = 0;
    uint64_t rtcp_nack_count_ = 0;
    uint64_t rtcp_transport_cc_count_ = 0;
    uint64_t rtcp_keyframe_requests_ = 0;
    bool ice_connected_ = false;
    bool dtls_connected_ = false;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_TRANSPORT_H_
