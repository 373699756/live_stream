#ifndef LIVE_STREAM_MEDIA_SRC_FLV_MUXER_H_
#define LIVE_STREAM_MEDIA_SRC_FLV_MUXER_H_

#include "frame_payload.h"
#include "media/media_streams.h"
#include "media_codec.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace media_internal {

constexpr size_t kMaxFlvVideoTagSlices =
    media_codec::kMaxNalUnitsPerFrame * 2 + 2;

struct FlvVideoTagBuild {
    MediaFlvVideoTagView view;
    size_t total_size = 0;
    uint8_t header[24] = {};
    uint8_t nal_lengths[kMaxFlvVideoTagSlices][4] = {};
    uint8_t previous_tag_size[4] = {};

    bool AddHeader(const uint8_t *data, size_t size) {
        return Add(data, size, false);
    }

    bool AddPayload(const uint8_t *data, size_t size) {
        return Add(data, size, true);
    }

private:
    bool Add(const uint8_t *data, size_t size, bool media_payload) {
        if (size == 0) {
            return true;
        }
        if (data == nullptr || view.slice_size >= kMaxFlvVideoTagSlices ||
            size > std::numeric_limits<size_t>::max() - total_size) {
            return false;
        }
        MediaFlvVideoTagSlice &slice = view.slices[view.slice_size];
        slice.data = data;
        slice.size = size;
        slice.media_payload = media_payload;
        total_size += size;
        ++view.slice_size;
        return true;
    }
};

class FlvMuxer {
public:
    static std::string BuildFileHeader();
    static std::string BuildSequenceHeader(Codec codec,
                                           const std::string &vps,
                                           const std::string &sps,
                                           const std::string &pps,
                                           uint32_t timestamp_ms);
    static bool BuildVideoTagView(const MediaFrame &frame,
                                  const FramePayload &payload,
                                  bool keyframe,
                                  FlvVideoTagBuild *tag);
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FLV_MUXER_H_
