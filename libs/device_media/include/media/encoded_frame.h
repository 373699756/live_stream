#ifndef LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
#define LIVE_STREAM_MEDIA_ENCODED_FRAME_H_

#include "media/media_buffer.h"
#include "media/stream_types.h"

#include <cstdint>

namespace live_stream {

struct EncodedFrame {
    StreamId stream_id = StreamId::kMain;
    VideoCodec codec = VideoCodec::kH264;
    FrameType frame_type = FrameType::kP;
    FrameSequence sequence = 0;
    int64_t pts_us = 0;
    int64_t dts_us = 0;
    VideoBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

inline void EncodedFrameInit(EncodedFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    frame->stream_id = StreamId::kMain;
    frame->codec = VideoCodec::kH264;
    frame->frame_type = FrameType::kP;
    frame->sequence = 0;
    frame->pts_us = 0;
    frame->dts_us = 0;
    frame->buffer = nullptr;
    frame->offset = 0;
    frame->size = 0;
}

inline BufferSlice EncodedFramePayloadSlice(const EncodedFrame *frame) {
    return frame == nullptr ? BufferSlice{}
                            : BufferSlice{frame->buffer, frame->offset,
                                          frame->size};
}

inline bool EncodedFrameHasPayload(const EncodedFrame *frame) {
    if (frame == nullptr || frame->size == 0) {
        return false;
    }
    return IsValidBufferSlice(EncodedFramePayloadSlice(frame));
}

inline const uint8_t *EncodedFramePayloadData(const EncodedFrame *frame) {
    return EncodedFrameHasPayload(frame)
               ? BufferSliceData(EncodedFramePayloadSlice(frame))
               : nullptr;
}

inline void EncodedFrameUnref(EncodedFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    VideoBufferUnref(frame->buffer);
    EncodedFrameInit(frame);
}

inline bool EncodedFrameRefCopy(EncodedFrame *target,
                                const EncodedFrame *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    VideoBuffer *retained = VideoBufferRef(source->buffer);
    if (source->buffer != nullptr && retained == nullptr) {
        return false;
    }
    EncodedFrameUnref(target);
    *target = *source;
    target->buffer = retained;
    return true;
}

inline bool EncodedFrameMove(EncodedFrame *target, EncodedFrame *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    EncodedFrameUnref(target);
    *target = *source;
    EncodedFrameInit(source);
    return true;
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
