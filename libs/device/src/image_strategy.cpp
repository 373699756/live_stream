#include "image_strategy.h"

#include "infra/clamp.h"
#include "json_reader.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace device_internal {
namespace {

constexpr int32_t kControlMin = 0;
constexpr int32_t kControlMax = 100;
constexpr int32_t kImageStrategyTierMin = 0;
constexpr int32_t kImageStrategyTierMax = 3;
constexpr uint32_t kImageStrategyMinTierSample = 1;
constexpr uint32_t kImageStrategyMaxTierSample = 10;
constexpr char kFieldFallbackExposureTimeDivisor[] =
    "fallback_exposure_time_divisor";
constexpr char kFieldGainBase[] = "gain_base";
constexpr char kFieldIsoTierThresholds[] = "iso_tier_thresholds";
constexpr char kFieldLowNoiseDenoise3dMax[] = "low_noise_denoise_3d_max";
constexpr char kFieldTierStabilitySamples[] = "tier_stability_samples";

struct ImageStrategyControls {
    int32_t saturation = 52;
    int32_t sharpness = 32;
    int32_t denoise_2d = 60;
    int32_t denoise_3d = 52;
    int32_t gamma = 50;
};

template <size_t kSize, typename T>
bool ParseIntegerArray(const Json &value, std::array<T, kSize> &out,
                      int64_t min_value, int64_t max_value) {
    if (!value.is_array() || value.size() != kSize) {
        return false;
    }
    std::array<T, kSize> parsed = {};
    for (size_t i = 0; i < kSize; ++i) {
        const Json &item = value.at(i);
        if (!item.is_number_integer() && !item.is_number_unsigned()) {
            return false;
        }
        int64_t converted = item.is_number_unsigned()
                                ? static_cast<int64_t>(item.get<uint64_t>())
                                : item.get<int64_t>();
        if (converted < min_value || converted > max_value) {
            return false;
        }
        parsed[i] = static_cast<T>(converted);
    }
    out = parsed;
    return true;
}

int ClampImageStrategyTier(int tier) {
    return infra::Clamp(tier, kImageStrategyTierMin, kImageStrategyTierMax);
}

int TierFromIso(uint32_t iso, const ImageStrategySettings &settings) {
    if (iso <= settings.iso_tier_thresholds[0]) {
        return 0;
    }
    if (iso <= settings.iso_tier_thresholds[1]) {
        return 1;
    }
    if (iso <= settings.iso_tier_thresholds[2]) {
        return 2;
    }
    return 3;
}

int DetermineImageStrategyTierWithSettings(
    const hisisdk::ExposureInfo &exposure,
    const ImageStrategySettings &settings) {
    if (exposure.iso != 0) {
        return TierFromIso(exposure.iso, settings);
    }

    uint64_t metric = exposure.exposure_time_us;
    if (metric == 0) {
        return kImageStrategyTierMin;
    }
    metric /= std::max<uint32_t>(1, settings.fallback_exposure_time_divisor);

    const uint32_t gain_base =
        std::max<uint32_t>(1, settings.gain_base);
    const uint64_t analog_gain =
        exposure.analog_gain == 0 ? gain_base : exposure.analog_gain;
    const uint64_t digital_gain =
        exposure.digital_gain == 0 ? gain_base : exposure.digital_gain;
    const uint64_t isp_digital_gain =
        exposure.isp_digital_gain == 0 ? gain_base
                                       : exposure.isp_digital_gain;
    metric = metric * analog_gain / gain_base;
    metric = metric * digital_gain / gain_base;
    metric = metric * isp_digital_gain / gain_base;

    return TierFromIso(
        std::min<uint64_t>(metric,
                           static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())),
                       settings);
}

const char *TierName(int tier) {
    switch (tier) {
        case 0:
            return "day";
        case 1:
            return "indoor";
        case 2:
            return "low_light";
        case 3:
            return "very_low_light";
    }
    return "unknown";
}

int32_t ClampImageControl(int32_t value) {
    return infra::Clamp(value, kControlMin, kControlMax);
}

int32_t ClampDenoise3dForLowNoise(
    int32_t value, int tier,
    const std::array<int32_t, 4> &low_noise_denoise_3d_max) {
    return infra::Clamp(
        value, kControlMin,
        low_noise_denoise_3d_max[ClampImageStrategyTier(tier)]);
}

ImageStrategyControls LoadImageStrategyControls(
    const Json &image_config) {
    ImageStrategyControls controls;
    const Json &basic = image_config.at("basic");
    const Json &enhancement = image_config.at("enhancement");
    (void)json_reader::ReadField(basic, "saturation", &controls.saturation,
                                kControlMin, kControlMax);
    (void)json_reader::ReadField(basic, "sharpness", &controls.sharpness,
                                kControlMin, kControlMax);
    (void)json_reader::ReadField(enhancement, "denoise_2d",
                                &controls.denoise_2d, kControlMin,
                                kControlMax);
    (void)json_reader::ReadField(enhancement, "denoise_3d",
                                &controls.denoise_3d, kControlMin,
                                kControlMax);
    (void)json_reader::ReadField(enhancement, "gamma", &controls.gamma,
                                kControlMin, kControlMax);
    return controls;
}

ImageStrategyControls ControlsForTier(const ImageStrategyControls &base,
                                     const std::string &mode, int tier,
                                     const std::array<int32_t, 4> &low_noise_denoise_3d_max) {
    int32_t saturation_delta[] = {0, 0, -6, -14};
    int32_t sharpness_delta[] = {0, -6, -14, -24};
    int32_t denoise_2d_delta[] = {0, 8, 18, 30};
    int32_t denoise_3d_delta[] = {0, 10, 22, 34};
    int32_t gamma_delta[] = {0, 2, 5, 8};
    bool low_noise_mode = false;
    if (mode == "low_noise") {
        low_noise_mode = true;
        saturation_delta[0] = 0;
        saturation_delta[1] = -2;
        saturation_delta[2] = -6;
        saturation_delta[3] = -12;
        sharpness_delta[0] = -2;
        sharpness_delta[1] = -4;
        sharpness_delta[2] = -10;
        sharpness_delta[3] = -16;
        denoise_2d_delta[0] = 4;
        denoise_2d_delta[1] = 10;
        denoise_2d_delta[2] = 18;
        denoise_2d_delta[3] = 26;
        denoise_3d_delta[0] = 4;
        denoise_3d_delta[1] = 12;
        denoise_3d_delta[2] = 26;
        denoise_3d_delta[3] = 36;
        gamma_delta[0] = 0;
        gamma_delta[1] = 1;
        gamma_delta[2] = 4;
        gamma_delta[3] = 7;
    } else if (mode == "detail") {
        saturation_delta[0] = 5;
        saturation_delta[1] = 2;
        saturation_delta[2] = -4;
        saturation_delta[3] = -10;
        sharpness_delta[0] = 8;
        sharpness_delta[1] = 2;
        sharpness_delta[2] = -8;
        sharpness_delta[3] = -18;
        denoise_2d_delta[0] = -6;
        denoise_2d_delta[1] = 2;
        denoise_2d_delta[2] = 12;
        denoise_2d_delta[3] = 24;
        denoise_3d_delta[0] = -4;
        denoise_3d_delta[1] = 4;
        denoise_3d_delta[2] = 16;
        denoise_3d_delta[3] = 28;
        gamma_delta[0] = 0;
        gamma_delta[1] = 3;
        gamma_delta[2] = 7;
        gamma_delta[3] = 10;
    }
    ImageStrategyControls controls;
    controls.saturation =
        ClampImageControl(base.saturation + saturation_delta[tier]);
    controls.sharpness =
        ClampImageControl(base.sharpness + sharpness_delta[tier]);
    controls.denoise_2d =
        ClampImageControl(base.denoise_2d + denoise_2d_delta[tier]);
    controls.denoise_3d =
        ClampImageControl(base.denoise_3d + denoise_3d_delta[tier]);
    controls.gamma = ClampImageControl(base.gamma + gamma_delta[tier]);
    if (low_noise_mode) {
        controls.denoise_3d = ClampDenoise3dForLowNoise(
            controls.denoise_3d, tier, low_noise_denoise_3d_max);
    }
    return controls;
}

ImageStrategyControls SmoothImageStrategyControls(
    const ImageStrategyControls &target,
    const ImageInfo &current) {
    ImageStrategyControls controls = target;
    if (!current.active) {
        return controls;
    }
    controls.saturation =
        current.saturation + (target.saturation - current.saturation) / 3;
    controls.sharpness =
        current.sharpness + (target.sharpness - current.sharpness) / 3;
    controls.denoise_2d =
        current.denoise_2d + (target.denoise_2d - current.denoise_2d) / 3;
    controls.denoise_3d =
        current.denoise_3d + (target.denoise_3d - current.denoise_3d) / 3;
    controls.gamma = current.gamma + (target.gamma - current.gamma) / 3;
    return controls;
}

ImageStrategyControls ClampFinalControlsForMode(
    const ImageStrategyControls &controls, const std::string &mode, int tier,
    const std::array<int32_t, 4> &low_noise_denoise_3d_max) {
    ImageStrategyControls clamped = controls;
    if (mode == "low_noise") {
        clamped.denoise_3d =
            ClampDenoise3dForLowNoise(clamped.denoise_3d, tier,
                                      low_noise_denoise_3d_max);
    }
    return clamped;
}

std::string ImageStrategyMode(const Json &image_config) {
    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return "low_noise";
    }
    return strategy->value("mode", std::string("low_noise"));
}

}  // namespace

bool IsImageStrategyEnabled(const Json &image_config) {
    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return true;
    }
    return strategy->value("enabled", true);
}

ImageStrategySettings LoadImageStrategySettings(const Json &image_config) {
    ImageStrategySettings settings;
    if (!image_config.is_object()) {
        return settings;
    }

    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return settings;
    }

    const Json &strategy_json = *strategy;

    uint32_t fallback_exposure_time_divisor = settings.fallback_exposure_time_divisor;
    if (json_reader::ReadField(strategy_json, kFieldFallbackExposureTimeDivisor,
                              &fallback_exposure_time_divisor)) {
        settings.fallback_exposure_time_divisor =
            std::max<uint32_t>(1, fallback_exposure_time_divisor);
    }

    uint32_t gain_base = settings.gain_base;
    if (json_reader::ReadField(strategy_json, kFieldGainBase, &gain_base)) {
        settings.gain_base = std::max<uint32_t>(1, gain_base);
    }
    const auto iso_tier_thresholds =
        strategy_json.find(kFieldIsoTierThresholds);
    if (iso_tier_thresholds != strategy_json.end()) {
        std::array<uint32_t, 3> thresholds;
        if (ParseIntegerArray(*iso_tier_thresholds, thresholds, 1,
                              std::numeric_limits<uint32_t>::max()) &&
            thresholds[0] <= thresholds[1] &&
            thresholds[1] <= thresholds[2]) {
            settings.iso_tier_thresholds = thresholds;
        }
    }

    const auto low_noise_denoise_3d_max =
        strategy_json.find(kFieldLowNoiseDenoise3dMax);
    if (low_noise_denoise_3d_max != strategy_json.end()) {
        std::array<int32_t, 4> limits;
        if (ParseIntegerArray(*low_noise_denoise_3d_max, limits, kControlMin,
                              kControlMax)) {
            settings.low_noise_denoise_3d_max = limits;
        }
    }

    int32_t tier_stability_samples = settings.tier_stability_samples;
    if (json_reader::ReadField(strategy_json, kFieldTierStabilitySamples,
                              &tier_stability_samples)) {
        settings.tier_stability_samples = infra::Clamp(
            tier_stability_samples, static_cast<int32_t>(kImageStrategyMinTierSample),
            static_cast<int32_t>(kImageStrategyMaxTierSample));
    }
    return settings;
}

int DetermineImageStrategyTier(const hisisdk::ExposureInfo &exposure) {
    return DetermineImageStrategyTier(exposure, ImageStrategySettings{});
}

int DetermineImageStrategyTier(const hisisdk::ExposureInfo &exposure,
                              const ImageStrategySettings &settings) {
    return DetermineImageStrategyTierWithSettings(exposure, settings);
}

Json BuildImageStrategyConfig(
    const Json &image_config,
    const ImageInfo &current_info,
    int strategy_tier,
    const hisisdk::ExposureInfo &exposure,
    ImageInfo &next_info,
    const ImageStrategySettings &settings) {
    Json adjusted = image_config;
    if (!adjusted.is_object()) {
        return adjusted;
    }

    const int tier = ClampImageStrategyTier(strategy_tier);
    const std::string strategy_mode = ImageStrategyMode(image_config);
    const ImageStrategyControls target =
        ControlsForTier(LoadImageStrategyControls(image_config), strategy_mode,
                        tier, settings.low_noise_denoise_3d_max);
    const ImageStrategyControls controls =
        ClampFinalControlsForMode(
            SmoothImageStrategyControls(target, current_info), strategy_mode,
            tier, settings.low_noise_denoise_3d_max);

    adjusted["basic"]["saturation"] = controls.saturation;
    adjusted["basic"]["sharpness"] = controls.sharpness;
    adjusted["enhancement"]["denoise_2d"] = controls.denoise_2d;
    adjusted["enhancement"]["denoise_3d"] = controls.denoise_3d;
    adjusted["enhancement"]["gamma"] = controls.gamma;

    next_info = current_info;
    next_info.enabled = true;
    next_info.active = true;
    next_info.exposure_valid = true;
    next_info.iso = exposure.iso;
    next_info.exposure_time_us = exposure.exposure_time_us;
    next_info.analog_gain = exposure.analog_gain;
    next_info.digital_gain = exposure.digital_gain;
    next_info.isp_digital_gain = exposure.isp_digital_gain;
    next_info.mode = strategy_mode;
    next_info.tier = TierName(tier);
    next_info.saturation = controls.saturation;
    next_info.sharpness = controls.sharpness;
    next_info.denoise_2d = controls.denoise_2d;
    next_info.denoise_3d = controls.denoise_3d;
    next_info.gamma = controls.gamma;
    return adjusted;
}

}  // namespace device_internal
}  // namespace live_stream
