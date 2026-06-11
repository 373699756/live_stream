#ifndef LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_
#define LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_

#include "config.h"
#include "device.h"
#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace device_internal {

bool IsImageStrategyEnabled(const ConfigJson &image_config);
ConfigJson BuildImageStrategyConfig(
    const ConfigJson &image_config,
    const ImageStrategyStatus &current_status,
    const hisisdk::ExposureInfo &exposure,
    ImageStrategyStatus *next_status);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_
