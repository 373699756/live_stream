#ifndef LIVE_STREAM_DEVICE_MEDIA_SRC_IMG_STRAT_H_
#define LIVE_STREAM_DEVICE_MEDIA_SRC_IMG_STRAT_H_

#include "config.h"
#include "device_media.h"
#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace device_media_internal {

bool IsImageStrategyEnabled(const ConfigJson &image_config);
ConfigJson BuildImageStrategyConfig(
    const ConfigJson &image_config,
    const ImageStrategyStatus &current_status,
    const hisisdk::ExposureInfo &exposure,
    ImageStrategyStatus *next_status);

}  // namespace device_media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_SRC_IMG_STRAT_H_
