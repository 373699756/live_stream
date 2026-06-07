#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_RTP_SENDER_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_RTP_SENDER_H_

#include "media_source.h"
#include "media_mux.h"
#include "webrtc.h"

#include <map>
#include <mutex>
#include <string>

namespace live_stream {
namespace webrtc_internal {

class IWebrtcEngine;
class WebrtcRtpPacketSink;

struct WebrtcRtpSenderContext {
    IWebrtcEngine *engine = nullptr;
    std::mutex *mutex = nullptr;
    WebrtcStats *service_stats = nullptr;
};

class WebrtcRtpSender {
public:
    explicit WebrtcRtpSender(uint32_t rtp_mtu_bytes);

    void AddPeer(const WebrtcPeerInfo &peer);
    void RemovePeer(const std::string &peer_id);
    void Clear();

    bool SendFrame(const WebrtcPeerInfo &peer, const MediaFrame &frame,
                   const WebrtcRtpSenderContext &context);

private:
    friend class WebrtcRtpPacketSink;

    struct PeerRtpState {
        uint16_t sequence = 1;
        uint32_t ssrc = 0;
        VideoCodec codec = VideoCodec::kH264;
        uint8_t payload_type = 0;
        uint32_t clock_rate = media_mux::kRtpClockRate;
        bool keyframe_seen = false;
    };

    bool SendRtpPacketView(const WebrtcPeerInfo &peer,
                           const EncodedFrame &frame,
                           const media_mux::RtpPacketView &packet,
                           const WebrtcRtpSenderContext &context);

    media_mux::RtpPacketizer packetizer_;
    std::map<std::string, PeerRtpState> peers_;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_RTP_SENDER_H_
