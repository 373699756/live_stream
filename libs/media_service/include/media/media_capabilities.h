#ifndef LIVE_STREAM_MEDIA_MEDIA_CAPABILITIES_H_
#define LIVE_STREAM_MEDIA_MEDIA_CAPABILITIES_H_

#include "infra/stream_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

struct VideoResolution {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct FrameRateRange {
    uint32_t min_fps = 1;
    uint32_t max_fps = 30;
};

struct BitrateRange {
    uint32_t min_kbps = 64;
    uint32_t max_kbps = 8192;
};

struct GopRange {
    uint32_t min = 1;
    uint32_t max = 120;
};

enum class RateControlMode {
    kCbr = 0,
    kVbr,
    kFixQp,
};

enum class GopMode {
    kNormalP = 0,
    kDualP,
    kSmartP,
};

struct CodecCapability {
    infra::VideoCodec codec = infra::VideoCodec::kH264;
    std::vector<std::string> profiles;
};

struct VideoStreamCapabilities {
    infra::StreamId stream_id = infra::StreamId::kMain;
    std::vector<CodecCapability> codecs;
    std::vector<VideoResolution> resolutions;
    FrameRateRange frame_rate;
    BitrateRange bitrate;
    std::vector<RateControlMode> rate_control_modes;
    GopRange gop;
    bool smart_codec_supported = false;
};

struct MediaCapabilities {
    std::vector<VideoStreamCapabilities> streams;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_CAPABILITIES_H_
