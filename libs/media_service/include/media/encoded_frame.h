#ifndef LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
#define LIVE_STREAM_MEDIA_ENCODED_FRAME_H_

#include "media/media_buffer.h"
#include "media/stream_types.h"

#include <cstdint>
#include <memory>

namespace live_stream {

struct EncodedFrame {
  StreamId stream_id = StreamId::kMain;
  VideoCodec codec = VideoCodec::kH264;
  FrameType frame_type = FrameType::kP;
  FrameSequence sequence = 0;
  int64_t pts_us = 0;
  int64_t dts_us = 0;
  std::shared_ptr<IMediaBuffer> buffer;
  uint32_t offset = 0;
  uint32_t size = 0;

  BufferSlice PayloadSlice() const {
    return BufferSlice{buffer, offset, size};
  }
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_ENCODED_FRAME_H_
