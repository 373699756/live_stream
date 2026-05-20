#include "webrtc_frame_dispatcher.h"

#include "stream_codec.h"

namespace live_stream {
namespace webrtc_internal {

void WebrtcFrameDispatcher::Clear() {
    main_pending_frame_ = PendingFrameSlot{};
    sub_pending_frame_ = PendingFrameSlot{};
    last_sent_stream_ = StreamId::kSub;
}

WebrtcFrameQueueResult WebrtcFrameDispatcher::Queue(
    const EncodedFrame &frame) {
    WebrtcFrameQueueResult result;
    PendingFrameSlot *slot = FindPendingSlot(frame.stream_id);
    if (slot == nullptr) {
        result.dropped_frames = 1;
        return result;
    }
    if (slot->ready) {
        const bool existing_keyframe =
            stream_codec::IsKeyFrame(slot->frame.frame_type);
        const bool new_keyframe = stream_codec::IsKeyFrame(frame.frame_type);
        if (existing_keyframe && !new_keyframe) {
            result.dropped_frames = 1;
            return result;
        }
        result.dropped_frames = 1;
    }
    slot->frame = frame;
    slot->ready = true;
    result.queued = true;
    return result;
}

bool WebrtcFrameDispatcher::TakeNext(EncodedFrame *frame) {
    if (frame == nullptr) {
        return false;
    }
    const StreamId preferred_stream = last_sent_stream_ == StreamId::kMain
                                          ? StreamId::kSub
                                          : StreamId::kMain;
    if (Take(preferred_stream, frame)) {
        return true;
    }
    const StreamId fallback_stream = preferred_stream == StreamId::kMain
                                         ? StreamId::kSub
                                         : StreamId::kMain;
    return Take(fallback_stream, frame);
}

WebrtcFrameDispatcher::PendingFrameSlot *
WebrtcFrameDispatcher::FindPendingSlot(StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
        return &main_pending_frame_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_pending_frame_;
    }
    return nullptr;
}

bool WebrtcFrameDispatcher::Take(StreamId stream_id, EncodedFrame *frame) {
    PendingFrameSlot *slot = FindPendingSlot(stream_id);
    if (slot == nullptr || frame == nullptr || !slot->ready) {
        return false;
    }
    *frame = slot->frame;
    slot->ready = false;
    last_sent_stream_ = stream_id;
    return true;
}

}  // namespace webrtc_internal
}  // namespace live_stream
