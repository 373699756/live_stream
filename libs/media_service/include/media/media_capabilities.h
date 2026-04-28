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

struct NumericControlCapability {
    std::string name;
    int32_t min = 0;
    int32_t max = 100;
    int32_t default_value = 50;
};

struct OptionControlCapability {
    std::string name;
    std::vector<std::string> values;
    std::string default_value;
};

struct ImageCapabilities {
    std::vector<NumericControlCapability> basic;
    std::vector<OptionControlCapability> exposure_options;
    std::vector<NumericControlCapability> exposure_ranges;
    std::vector<OptionControlCapability> white_balance_options;
    std::vector<NumericControlCapability> white_balance_ranges;
    std::vector<NumericControlCapability> enhancement_ranges;
    std::vector<OptionControlCapability> enhancement_options;
    std::vector<OptionControlCapability> backlight_options;
    std::vector<NumericControlCapability> backlight_ranges;
    std::vector<OptionControlCapability> color_mode_options;
    bool mirror_supported = true;
    bool flip_supported = true;
};

struct MediaCapabilities {
    std::vector<VideoStreamCapabilities> streams;
    ImageCapabilities image;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_CAPABILITIES_H_
