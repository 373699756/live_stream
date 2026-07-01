#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_VIDEO_SENDER_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_VIDEO_SENDER_H_

#include "media/media_streams.h"
#include "webrtc.h"
#include "webrtc_peer_host.h"
#include "webrtc_peer_table.h"
#include "webrtc_rtp_sender.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace live_stream {

struct WebrtcCallbackGuard;

namespace event {
class Loop;
}

class WebrtcPeerVideoSender {
public:
    struct ClosedVideo {
        FrameSubscriptionId subscription_id = 0;
        event::TimerId send_timer_id = 0;
        std::deque<MediaFrame> start_frames;
    };

    WebrtcPeerVideoSender(
        MediaStreams *media_streams,
        event::Loop *net_loop,
        std::shared_ptr<WebrtcCallbackGuard> callback_guard,
        std::mutex *mutex,
        webrtc_internal::WebrtcPeerTable *peer_table,
        std::shared_ptr<webrtc_internal::IWebrtcPeerHost> *peer_host,
        WebrtcStats *stats);

    bool OpenPeerVideo(const std::string &peer_id);
    void ClosePeerVideo(const std::string &peer_id, SubscriptionClose reason);
    std::vector<ClosedVideo> CloseAllPeerVideo();
    void ReleaseClosedPeerVideo(std::vector<ClosedVideo> *closed_video,
                                SubscriptionClose reason);
    void FillPeerVideoInfo(WebrtcPeerInfo *peer) const;

private:
    struct PeerVideo {
        FrameSubscriptionId subscription_id = 0;
        uint64_t generation = 0;
        MediaStreamInfo track;
        std::deque<MediaFrame> start_frames;
        event::TimerId send_timer_id = 0;
        bool sending = false;
        bool closing = false;
    };

    static void DispatchPeerVideoSend(
        const std::shared_ptr<WebrtcCallbackGuard> &callback_guard,
        WebrtcPeerVideoSender *sender,
        const std::string &peer_id);

    void ArmPeerSendTimer(const std::string &peer_id);
    void SendPeerFrames(const std::string &peer_id);
    bool BeginPeerSend(const std::string &peer_id);
    void EndPeerSend(const std::string &peer_id);
    bool SendPeerStartFrames(const std::string &peer_id,
                             uint32_t *sent_frames);
    void SendPeerFrame(const std::string &peer_id,
                       const MediaFrame &frame);
    ClosedVideo TakePeerVideo(const std::string &peer_id);
    ClosedVideo TakePeerVideoLocked(const std::string &peer_id);
    void ReleaseClosedPeerVideo(ClosedVideo *closed_video,
                                SubscriptionClose reason);
    static void ClearMediaFrames(std::deque<MediaFrame> *frames);

    MediaStreams *media_streams_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    std::shared_ptr<WebrtcCallbackGuard> callback_guard_;
    std::mutex *mutex_ = nullptr;
    webrtc_internal::WebrtcPeerTable *peer_table_ = nullptr;
    std::shared_ptr<webrtc_internal::IWebrtcPeerHost> *peer_host_ = nullptr;
    WebrtcStats *stats_ = nullptr;
    std::condition_variable video_condition_;
    std::map<std::string, PeerVideo> peer_video_;
    webrtc_internal::WebrtcRtpSender rtp_sender_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_VIDEO_SENDER_H_
