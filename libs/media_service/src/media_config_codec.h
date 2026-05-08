#ifndef LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_CONFIG_CODEC_H_
#define LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_CONFIG_CODEC_H_

#include "config_service.h"
#include "live_stream/config_json.h"
#include "media/media_capabilities.h"
#include "media/pipeline_config.h"

namespace live_stream {
namespace media_internal {

ConfigResult ParseVideoConfig(const ConfigJson &value,
                              const MediaPipelineConfig &fallback,
                              const MediaCapabilities &capabilities,
                              MediaPipelineConfig *parsed);
ConfigResult ValidateImageConfig(const ConfigJson &value,
                                 const ImageCapabilities &capabilities);

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_CONFIG_CODEC_H_
