#ifndef LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_VALUES_H_
#define LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_VALUES_H_

#include "hisi_vendor/media_pipeline.h"

#include <string>

namespace live_stream {
namespace media_config_values {

bool ParseResolutionText(const std::string &text, VideoSize *size);
bool ParseCodecText(const std::string &codec, Codec *parsed);
const char *CodecToString(Codec codec);
bool ParseRateControlText(const std::string &rc_mode,
                          RateControlMode *parsed);
const char *RateControlToString(RateControlMode mode);
bool ParseGopModeText(const std::string &gop_mode, GopMode *parsed);
const char *GopModeToString(GopMode mode);

}  // namespace media_config_values
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_MEDIA_CONFIG_VALUES_H_
