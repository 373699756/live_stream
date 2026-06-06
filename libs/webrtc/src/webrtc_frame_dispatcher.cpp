#include "webrtc_frame_dispatcher.h"

#include "media_codec.h"

namespace live_stream {
namespace webrtc_internal {

void WebrtcFrameDispatcher::Clear() {
    FramePayloadUnref(&main_pending_frame_.frame);
    FramePayloadUnref(&sub_pending_frame_.frame);
    main_pending_frame_.ready = false;
    sub_pending_frame_.ready = false;
    last_sent_stream_ = StreamId::kSub;
}

WebrtcFrameQueueResult WebrtcFrameDispatcher::Queue(
    const FramePayload &frame) {
    WebrtcFrameQueueResult result;
    PendingFrameSlot *slot = FindPendingSlot(frame.encoded_frame.stream_id);
    if (slot == nullptr) {
        result.dropped_frames = 1;
        return result;
    }
    if (slot->ready) {
        const bool existing_keyframe =
            media_codec::IsKeyFrame(slot->frame.encoded_frame.frame_type);
        const bool new_keyframe =
            media_codec::IsKeyFrame(frame.encoded_frame.frame_type);
        if (existing_keyframe && !new_keyframe) {
            result.dropped_frames = 1;
            return result;
        }
        result.dropped_frames = 1;
    }
    if (!FramePayloadRefCopy(&slot->frame, &frame)) {
        result.dropped_frames = 1;
        return result;
    }
    slot->ready = true;
    result.queued = true;
    return result;
}

bool WebrtcFrameDispatcher::TakeNext(FramePayload *frame) {
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

bool WebrtcFrameDispatcher::Take(StreamId stream_id, FramePayload *frame) {
    PendingFrameSlot *slot = FindPendingSlot(stream_id);
    if (slot == nullptr || frame == nullptr || !slot->ready) {
        return false;
    }
    if (!FramePayloadMove(frame, &slot->frame)) {
        return false;
    }
    slot->ready = false;
    last_sent_stream_ = stream_id;
    return true;
}

}  // namespace webrtc_internal
}  // namespace live_stream
