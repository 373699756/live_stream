#ifndef LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
#define LIVE_STREAM_MEDIA_ENCODED_FRAME_H_

#include "media/media_buffer.h"
#include "media/stream_types.h"

#include <cstdint>

namespace live_stream {

struct EncodedFrame {
    // 从 hisi_vendor 输出后，buffer 指向项目自己的 VideoBuffer，而不是
    // HiSilicon VENC 内部 pack 内存。offset/size 描述当前帧在 buffer 中的 payload。
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
    // 帧对象拷贝只增加 VideoBuffer 引用计数，不复制编码码流 payload。
    // 这保证 media_pipeline、media_source、HTTP/WebRTC 多路扇出不会按客户端数
    // 放大整帧内存。
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
    // move 转移 buffer owner，不增加引用计数；source 会被清空，避免双重 unref。
    EncodedFrameUnref(target);
    *target = *source;
    EncodedFrameInit(source);
    return true;
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
