#ifndef LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
#define LIVE_STREAM_MEDIA_ENCODED_FRAME_H_

#include "media/media_buffer.h"
#include "media/stream_types.h"

#include <cstdint>

namespace live_stream {

struct EncodedFrame {
    // 从设备层输出后，payload.buffer 指向项目自己的 FrameBuffer，而不是
    // 硬件编码器内部 pack 内存。FrameSlice 描述当前帧在 buffer 中的 payload。
    StreamId stream_id = StreamId::kMain;
    Codec codec = Codec::kH264;
    FrameType frame_type = FrameType::kP;
    FrameSequence sequence = 0;
    int64_t pts_us = 0;
    int64_t dts_us = 0;
    // duration_us 由 MediaStreams 在订阅/GOP 输出时按连续帧时间戳估算；
    // 设备侧刚输出的帧可以保持 0。
    int64_t duration_us = 0;
    FrameSlice payload;
};

inline void EncodedFrameInit(EncodedFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    frame->stream_id = StreamId::kMain;
    frame->codec = Codec::kH264;
    frame->frame_type = FrameType::kP;
    frame->sequence = 0;
    frame->pts_us = 0;
    frame->dts_us = 0;
    frame->duration_us = 0;
    frame->payload = FrameSlice{};
}

inline FrameSlice EncodedFramePayloadSlice(const EncodedFrame *frame) {
    return frame == nullptr ? FrameSlice{} : frame->payload;
}

inline bool IsEncodedFramePayloadValid(const EncodedFrame *frame) {
    if (frame == nullptr || frame->payload.size == 0) {
        return false;
    }
    return IsValidFrameSlice(EncodedFramePayloadSlice(frame));
}

inline const uint8_t *EncodedFramePayloadData(const EncodedFrame *frame) {
    return IsEncodedFramePayloadValid(frame)
               ? FrameSliceData(EncodedFramePayloadSlice(frame))
               : nullptr;
}

inline void EncodedFrameUnref(EncodedFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    FrameBufferUnref(frame->payload.buffer);
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
    // 帧对象拷贝只增加 FrameBuffer 引用计数，不复制编码码流 payload。
    // 这保证媒体分发、HTTP/WebRTC 多路扇出不会按客户端数
    // 放大整帧内存。
    FrameBuffer *retained = FrameBufferRef(source->payload.buffer);
    if (source->payload.buffer != nullptr && retained == nullptr) {
        return false;
    }
    EncodedFrameUnref(target);
    *target = *source;
    target->payload.buffer = retained;
    return true;
}

inline bool EncodedFrameMove(EncodedFrame *target, EncodedFrame *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    // move 转移 buffer owner，不增加引用计数；source 会被清空，避免双重 unref。
    EncodedFrameUnref(target);
    *target = *source;
    EncodedFrameInit(source);
    return true;
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
