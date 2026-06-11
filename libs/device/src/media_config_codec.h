#ifndef LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_CODEC_H_
#define LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_CODEC_H_

#include "config.h"
#include "config_json.h"
#include "media/media_capabilities.h"
#include "media/pipeline_config.h"

#include <cstdint>

namespace live_stream {

void to_json(ConfigJson &json, const VideoSize &size);
void from_json(const ConfigJson &json, VideoSize &size);
void to_json(ConfigJson &json, const Codec &codec);
void from_json(const ConfigJson &json, Codec &codec);
void to_json(ConfigJson &json, const RateControlMode &mode);
void from_json(const ConfigJson &json, RateControlMode &mode);
void to_json(ConfigJson &json, const GopMode &mode);
void from_json(const ConfigJson &json, GopMode &mode);
void to_json(ConfigJson &json, const VideoRoiRegion &region);
void from_json(const ConfigJson &json, VideoRoiRegion &region);
void to_json(ConfigJson &json, const VideoRoiConfig &roi);
void from_json(const ConfigJson &json, VideoRoiConfig &roi);

namespace media_internal {

struct VideoConfig {
    struct Stream {
        bool enabled = false;
        Codec codec = Codec::kH264;
        VideoSize resolution;
        uint32_t fps = 0;
        uint32_t bitrate_kbps = 0;
        RateControlMode rate_control = RateControlMode::kCbr;
        uint32_t gop = 0;
        GopMode gop_mode = GopMode::kNormalP;
        bool smart_codec = false;
        VideoRoiConfig roi;
    };

    Stream main;
    Stream sub;
};

void to_json(ConfigJson &json, const VideoConfig::Stream &stream);
void from_json(const ConfigJson &json, VideoConfig::Stream &stream);
void to_json(ConfigJson &json, const VideoConfig &config);
void from_json(const ConfigJson &json, VideoConfig &config);

ConfigResult DecodeVideoConfig(const ConfigJson &value, VideoConfig *config);
ConfigResult ValidateVideoConfig(const VideoConfig &config,
                                 const MediaCapabilities &capabilities);
ConfigResult BuildPipelineConfig(const VideoConfig &config,
                                 const MediaPipelineConfig &fallback,
                                 MediaPipelineConfig *pipeline_config);
ConfigResult ParseVideoConfig(const ConfigJson &value,
                              const MediaPipelineConfig &fallback,
                              const MediaCapabilities &capabilities,
                              MediaPipelineConfig *parsed);
ConfigResult ValidateImageConfig(const ConfigJson &value,
                                 const ImageCapabilities &capabilities,
                                 const MediaPipelineConfig &active_config);

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_CODEC_H_
