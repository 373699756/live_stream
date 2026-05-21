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

    EncodedFrame() = default;

    EncodedFrame(const EncodedFrame& other)
        : stream_id(other.stream_id),
          codec(other.codec),
          frame_type(other.frame_type),
          sequence(other.sequence),
          pts_us(other.pts_us),
          dts_us(other.dts_us),
          buffer(VideoBufferRetain(other.buffer)),
          offset(other.offset),
          size(other.size) {}

    EncodedFrame& operator=(const EncodedFrame& other) {
        if (this == &other) {
            return *this;
        }
        VideoBuffer* retained = VideoBufferRetain(other.buffer);
        VideoBufferRelease(buffer);
        stream_id = other.stream_id;
        codec = other.codec;
        frame_type = other.frame_type;
        sequence = other.sequence;
        pts_us = other.pts_us;
        dts_us = other.dts_us;
        buffer = retained;
        offset = other.offset;
        size = other.size;
        return *this;
    }

    EncodedFrame(EncodedFrame&& other) noexcept
        : stream_id(other.stream_id),
          codec(other.codec),
          frame_type(other.frame_type),
          sequence(other.sequence),
          pts_us(other.pts_us),
          dts_us(other.dts_us),
          buffer(other.buffer),
          offset(other.offset),
          size(other.size) {
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
    }

    EncodedFrame& operator=(EncodedFrame&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        VideoBufferRelease(buffer);
        stream_id = other.stream_id;
        codec = other.codec;
        frame_type = other.frame_type;
        sequence = other.sequence;
        pts_us = other.pts_us;
        dts_us = other.dts_us;
        buffer = other.buffer;
        offset = other.offset;
        size = other.size;
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
        return *this;
    }

    ~EncodedFrame() { VideoBufferRelease(buffer); }

    BufferSlice PayloadSlice() const {
        return BufferSlice{buffer, offset, size};
    }

    bool HasValidPayload() const {
        return size != 0 && IsValidBufferSlice(PayloadSlice());
    }

    const uint8_t *PayloadData() const {
        return HasValidPayload() ? BufferSliceData(PayloadSlice()) : nullptr;
    }
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
