#ifndef LIVE_STREAM_MEDIA_MEDIA_FRAME_H_
#define LIVE_STREAM_MEDIA_MEDIA_FRAME_H_

#include "media/media_buffer.h"
#include "media/stream_types.h"

#include <cstdint>

namespace live_stream {

struct MediaFrame {
    // MediaFrame is one encoded video frame in project-owned memory. Copying the
    // value keeps the same payload alive through MediaBufferRef; it does not
    // copy encoded bytes.
    StreamId stream_id = StreamId::kMain;
    Codec codec = Codec::kH264;
    FrameType frame_type = FrameType::kP;
    FrameSequence sequence = 0;
    int64_t pts_us = 0;
    int64_t dts_us = 0;
    int64_t duration_us = 0;
    MediaBufferRef payload;
};

inline bool IsMediaFramePayloadValid(const MediaFrame& frame) {
    return frame.payload.Valid() && frame.payload.Size() != 0;
}

inline const uint8_t* MediaFramePayloadData(const MediaFrame& frame) {
    return IsMediaFramePayloadValid(frame) ? frame.payload.Data() : nullptr;
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_FRAME_H_
