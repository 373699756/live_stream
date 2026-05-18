#ifndef LIVE_STREAM_MEDIA_STREAM_TYPES_H_
#define LIVE_STREAM_MEDIA_STREAM_TYPES_H_

#include <cstdint>

namespace live_stream {

enum class StreamId {
    kMain = 0,
    kSub,
    kSnapshot,
};

enum class VideoCodec {
    kH264 = 0,
    kH265,
    kMjpeg,
    kJpeg,
};

enum class FrameType {
    kIdr = 0,
    kI,
    kP,
    kB,
    kJpeg,
};

using FrameSequence = uint64_t;

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_STREAM_TYPES_H_
