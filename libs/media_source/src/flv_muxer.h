#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_FLV_MUXER_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_FLV_MUXER_H_

#include "media/frame_attach.h"
#include "media_mux.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace media_source_internal {

using FlvVideoTagView = stream_mux::FlvVideoTagView;

class FlvMuxer {
public:
    static std::string BuildFileHeader();
    static std::string BuildSequenceHeader(VideoCodec codec,
                                           const std::string &vps,
                                           const std::string &sps,
                                           const std::string &pps,
                                           uint32_t timestamp_ms);
    static bool BuildVideoTagView(const EncodedFrame &frame,
                                  const FramePayload &payload,
                                  bool keyframe,
                                  FlvVideoTagView *tag_view);
};

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_FLV_MUXER_H_
