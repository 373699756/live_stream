#include "image_strategy.h"

#include "infra/clamp.h"
#include "json_utils.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace device_media_internal {
namespace {

constexpr int32_t kControlMin = 0;
constexpr int32_t kControlMax = 100;

struct ImageStrategyControls {
    int32_t saturation = 52;
    int32_t sharpness = 32;
    int32_t denoise_2d = 60;
    int32_t denoise_3d = 52;
    int32_t gamma = 50;
};

int IsoTier(uint32_t iso) {
    if (iso <= 400) {
        return 0;
    }
    if (iso <= 1600) {
        return 1;
    }
    if (iso <= 6400) {
        return 2;
    }
    return 3;
}

const char *IsoTierName(int tier) {
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

int32_t ClampDenoise3dForLowNoise(int32_t value, int tier) {
    constexpr int32_t kLowNoiseDenoise3dMax[] = {56, 66, 84, 88};
    return infra::Clamp(value, kControlMin, kLowNoiseDenoise3dMax[tier]);
}

ImageStrategyControls LoadImageStrategyControls(
    const ConfigJson &image_config) {
    ImageStrategyControls controls;
    const ConfigJson &basic = image_config.at("basic");
    const ConfigJson &enhancement = image_config.at("enhancement");
    (void)json_utils::ReadField(basic, "saturation", &controls.saturation,
                                kControlMin, kControlMax);
    (void)json_utils::ReadField(basic, "sharpness", &controls.sharpness,
                                kControlMin, kControlMax);
    (void)json_utils::ReadField(enhancement, "denoise_2d",
                                &controls.denoise_2d, kControlMin,
                                kControlMax);
    (void)json_utils::ReadField(enhancement, "denoise_3d",
                                &controls.denoise_3d, kControlMin,
                                kControlMax);
    (void)json_utils::ReadField(enhancement, "gamma", &controls.gamma,
                                kControlMin, kControlMax);
    return controls;
}

ImageStrategyControls ControlsForIsoTier(
    const ImageStrategyControls &base, const std::string &mode, int tier) {
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
    if (low_noise_mode) {
        controls.denoise_3d =
            ClampDenoise3dForLowNoise(controls.denoise_3d, tier);
    }
    controls.gamma = ClampImageControl(base.gamma + gamma_delta[tier]);
    return controls;
}

ImageStrategyControls SmoothImageStrategyControls(
    const ImageStrategyControls &target,
    const ImageStrategyStatus &current) {
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
    const ImageStrategyControls &controls, const std::string &mode, int tier) {
    ImageStrategyControls clamped = controls;
    if (mode == "low_noise") {
        clamped.denoise_3d =
            ClampDenoise3dForLowNoise(clamped.denoise_3d, tier);
    }
    return clamped;
}

std::string ImageStrategyMode(const ConfigJson &image_config) {
    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return "low_noise";
    }
    return strategy->value("mode", std::string("low_noise"));
}

}  // namespace

bool IsImageStrategyEnabled(const ConfigJson &image_config) {
    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return true;
    }
    return strategy->value("enabled", true);
}

ConfigJson BuildImageStrategyConfig(
    const ConfigJson &image_config,
    const ImageStrategyStatus &current_status,
    const hisisdk::ExposureInfo &exposure,
    ImageStrategyStatus *next_status) {
    ConfigJson adjusted = image_config;
    if (!adjusted.is_object()) {
        return adjusted;
    }

    const int tier = IsoTier(exposure.iso);
    const std::string strategy_mode = ImageStrategyMode(image_config);
    const ImageStrategyControls target = ControlsForIsoTier(
        LoadImageStrategyControls(image_config), strategy_mode, tier);
    const ImageStrategyControls controls = ClampFinalControlsForMode(
        SmoothImageStrategyControls(target, current_status), strategy_mode,
        tier);

    adjusted["basic"]["saturation"] = controls.saturation;
    adjusted["basic"]["sharpness"] = controls.sharpness;
    adjusted["enhancement"]["denoise_2d"] = controls.denoise_2d;
    adjusted["enhancement"]["denoise_3d"] = controls.denoise_3d;
    adjusted["enhancement"]["gamma"] = controls.gamma;

    if (next_status != nullptr) {
        *next_status = current_status;
        next_status->enabled = true;
        next_status->active = true;
        next_status->exposure_valid = true;
        next_status->iso = exposure.iso;
        next_status->exposure_time_us = exposure.exposure_time_us;
        next_status->analog_gain = exposure.analog_gain;
        next_status->digital_gain = exposure.digital_gain;
        next_status->isp_digital_gain = exposure.isp_digital_gain;
        next_status->mode = strategy_mode;
        next_status->tier = IsoTierName(tier);
        next_status->saturation = controls.saturation;
        next_status->sharpness = controls.sharpness;
        next_status->denoise_2d = controls.denoise_2d;
        next_status->denoise_3d = controls.denoise_3d;
        next_status->gamma = controls.gamma;
    }
    return adjusted;
}

}  // namespace device_media_internal
}  // namespace live_stream
