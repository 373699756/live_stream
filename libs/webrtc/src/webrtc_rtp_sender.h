#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_RTP_SENDER_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_RTP_SENDER_H_

#include "media/encoded_frame.h"
#include "rtp.h"
#include "webrtc.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace live_stream {
namespace webrtc_internal {

class IWebrtcEngine;
class WebrtcRtpPacketSink;

struct WebrtcRtpSenderContext {
    std::shared_ptr<IWebrtcEngine> engine;
    std::mutex *mutex = nullptr;
    WebrtcStats *service_stats = nullptr;
};

class WebrtcRtpSender {
public:
    explicit WebrtcRtpSender(uint32_t rtp_mtu_bytes);

    void AddPeer(const WebrtcPeerInfo &peer);
    void RemovePeer(const std::string &peer_id);
    void Clear();

    bool SendFrame(const WebrtcPeerInfo &peer, const EncodedFrame &frame,
                   const WebrtcRtpSenderContext &context);

private:
    friend class WebrtcRtpPacketSink;

    struct PeerRtpState {
        uint16_t sequence = 1;
        uint32_t ssrc = 0;
        Codec codec = Codec::kH264;
        uint8_t payload_type = 0;
        uint32_t clock_rate = rtp::kRtpClockRate;
        bool keyframe_seen = false;
        uint32_t last_rtp_timestamp = 0;
        bool has_last_rtp_timestamp = false;
    };

    bool SendRtpPacketView(const WebrtcPeerInfo &peer,
                           const EncodedFrame &frame,
                           const rtp::RtpPacketView &packet,
                           const WebrtcRtpSenderContext &context);

    rtp::RtpPacketizer packetizer_;
    std::map<std::string, PeerRtpState> peers_;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_RTP_SENDER_H_
