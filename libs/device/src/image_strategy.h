#ifndef LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_
#define LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_

#include "config.h"
#include "device.h"
#include "hisi_vendor/sdk.h"

namespace live_stream {
namespace device_internal {

bool IsImageStrategyEnabled(const Json &image_config);
Json BuildImageStrategyConfig(
    const Json &image_config,
    const ImageInfo &current_info,
    const hisisdk::ExposureInfo &exposure,
    ImageInfo &next_info);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_
