#ifndef LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_CODEC_H_
#define LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_CODEC_H_

#include "config.h"
#include "json.h"
#include "hisi_vendor/media_capabilities.h"
#include "hisi_vendor/media_pipeline.h"

#include <cstdint>

namespace live_stream {

void to_json(Json &json, const VideoSize &size);
void from_json(const Json &json, VideoSize &size);
void to_json(Json &json, const Codec &codec);
void from_json(const Json &json, Codec &codec);
void to_json(Json &json, const RateControlMode &mode);
void from_json(const Json &json, RateControlMode &mode);
void to_json(Json &json, const GopMode &mode);
void from_json(const Json &json, GopMode &mode);
void to_json(Json &json, const VideoRoiRegion &region);
void from_json(const Json &json, VideoRoiRegion &region);
void to_json(Json &json, const VideoRoiConfig &roi);
void from_json(const Json &json, VideoRoiConfig &roi);

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

void to_json(Json &json, const VideoConfig::Stream &stream);
void from_json(const Json &json, VideoConfig::Stream &stream);
void to_json(Json &json, const VideoConfig &config);
void from_json(const Json &json, VideoConfig &config);

ConfigCode DecodeVideoConfig(const Json &value,
                               VideoConfig *config,
                               ConfigError *error);
ConfigCode VerifyVideoConfig(const VideoConfig &config,
                               const MediaCapabilities &capabilities,
                               ConfigError *error);
ConfigCode BuildPipelineConfig(const VideoConfig &config,
                                 const MediaPipelineConfig &fallback,
                                 MediaPipelineConfig *pipeline_config,
                                 ConfigError *error);
ConfigCode ParseVideoConfig(const Json &value,
                              const MediaPipelineConfig &fallback,
                              const MediaCapabilities &capabilities,
                              MediaPipelineConfig *parsed,
                              ConfigError *error);
ConfigCode VerifyImageConfig(const Json &value,
                               const ImageCapabilities &capabilities,
                               const MediaPipelineConfig &active_config,
                               ConfigError *error);

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_CODEC_H_
