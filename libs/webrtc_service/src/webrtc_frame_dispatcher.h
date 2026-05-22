#ifndef LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_FRAME_DISPATCHER_H_
#define LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_FRAME_DISPATCHER_H_

#include "media/frame_subscription.h"

namespace live_stream {
namespace webrtc_internal {

struct WebrtcFrameQueueResult {
    bool queued = false;
    uint64_t dropped_frames = 0;
};

// Keeps the latest pending frame for each WebRTC stream and drains streams
// fairly. The owning service provides synchronization.
class WebrtcFrameDispatcher {
public:
    void Clear();
    WebrtcFrameQueueResult Queue(const ParsedVideoFrame &frame);
    bool TakeNext(ParsedVideoFrame *frame);

private:
    struct PendingFrameSlot {
        ParsedVideoFrame frame;
        bool ready = false;
    };

    PendingFrameSlot *FindPendingSlot(StreamId stream_id);
    bool Take(StreamId stream_id, ParsedVideoFrame *frame);

    PendingFrameSlot main_pending_frame_;
    PendingFrameSlot sub_pending_frame_;
    StreamId last_sent_stream_ = StreamId::kSub;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_FRAME_DISPATCHER_H_
