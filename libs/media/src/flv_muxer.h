#ifndef LIVE_STREAM_MEDIA_SRC_FLV_MUXER_H_
#define LIVE_STREAM_MEDIA_SRC_FLV_MUXER_H_

#include "frame_payload.h"
#include "media_codec.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace media_internal {

constexpr size_t kMaxFlvVideoTagSlices =
    media_codec::kMaxNalUnitsPerFrame * 2 + 2;

struct FlvVideoTagSlice {
    // tag view 以 slice 方式描述 FLV tag：小 header 存在本对象内部，
    // 大块视频 payload 直接引用 MediaFrame，发送端按 slice 顺序写出即可。
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool media_payload = false;
};

struct FlvVideoTagView {
    FlvVideoTagSlice slices[kMaxFlvVideoTagSlices];
    size_t slice_count = 0;
    size_t total_size = 0;
    uint32_t timestamp_ms = 0;
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
        if (data == nullptr || slice_count >= kMaxFlvVideoTagSlices ||
            size > std::numeric_limits<size_t>::max() - total_size) {
            return false;
        }
        slices[slice_count].data = data;
        slices[slice_count].size = size;
        slices[slice_count].media_payload = media_payload;
        total_size += size;
        ++slice_count;
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
                                  FlvVideoTagView *tag_view);
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FLV_MUXER_H_
