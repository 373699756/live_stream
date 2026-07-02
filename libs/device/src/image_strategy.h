#ifndef LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_
#define LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_

#include <array>

#include "config.h"
#include "device.h"
#include "hisi_vendor/sdk.h"

namespace live_stream {
namespace device_internal {

struct ImageStrategySettings {
    std::array<uint32_t, 3> iso_tier_thresholds = {400, 1600, 6400};
    uint32_t fallback_exposure_time_divisor = 100;
    uint32_t gain_base = 0x400;
    std::array<int32_t, 4> low_noise_denoise_3d_max = {56, 66, 84, 88};
    int32_t tier_stability_samples = 2;
};

bool IsImageStrategyEnabled(const Json &image_config);
ImageStrategySettings LoadImageStrategySettings(const Json &image_config);
int DetermineImageStrategyTier(const hisisdk::ExposureInfo &exposure);
int DetermineImageStrategyTier(const hisisdk::ExposureInfo &exposure,
                              const ImageStrategySettings &settings);
Json BuildImageStrategyConfig(
    const Json &image_config,
    const ImageInfo &current_info,
    int strategy_tier,
    const hisisdk::ExposureInfo &exposure,
    ImageInfo &next_info,
    const ImageStrategySettings &settings);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_IMAGE_STRATEGY_H_
