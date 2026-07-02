#include "media_config_codec.h"

#include "config_error.h"
#include "json_reader.h"

#include <array>
#include <string>
#include <vector>

namespace live_stream {
namespace media_internal {
namespace {

bool ContainsString(const std::vector<std::string> &values,
                    const std::string &value) {
    for (const std::string &item : values) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

ConfigCode ValidateNumericControls(
    const Json &section,
    const std::string &section_name,
    const std::vector<NumericControlCapability> &controls,
    ConfigError *error) {
    for (const NumericControlCapability &control : controls) {
        int64_t value = 0;
        const std::string field = JoinField(section_name, control.name);
        if (!json_reader::ReadField(section, control.name.c_str(), &value,
                                    control.min, control.max)) {
            return MakeConfigError(field, "missing or unsupported value", error);
        }
    }
    return ConfigCode::kOk;
}

ConfigCode ValidateOptionControls(
    const Json &section,
    const std::string &section_name,
    const std::vector<OptionControlCapability> &controls,
    ConfigError *error) {
    for (const OptionControlCapability &control : controls) {
        const std::string field = JoinField(section_name, control.name);
        std::string value;
        if (json_reader::ReadField(section, control.name.c_str(), &value)) {
            if (!ContainsString(control.values, value)) {
                return MakeConfigError(field, "unsupported value", error);
            }
            continue;
        }
        bool enabled = false;
        if (!json_reader::ReadField(section, control.name.c_str(), &enabled)) {
            return MakeConfigError(field, "missing or invalid value", error);
        }
        if (!ContainsString(control.values, enabled ? "true" : "false")) {
            return MakeConfigError(field, "unsupported value", error);
        }
    }
    return ConfigCode::kOk;
}

ConfigCode ValidateLensCorrectionConfig(
    const Json &value,
    const ImageCapabilities &capabilities,
    const MediaPipelineConfig &active_config,
    ConfigError *error) {
    if (!value.contains("lens_correction")) {
        return ConfigCode::kOk;
    }
    if (!value.at("lens_correction").is_object()) {
        return MakeConfigError("lens_correction",
                               "missing or invalid value", error);
    }
    const Json &lens_correction = value.at("lens_correction");
    bool enabled = false;
    if (!json_reader::ReadField(lens_correction, "enabled", &enabled)) {
        return MakeConfigError("lens_correction.enabled",
                               "missing or invalid value", error);
    }
    if (enabled && !capabilities.lens_correction_supported) {
        return MakeConfigError("lens_correction.enabled",
                               "unsupported value", error);
    }
    if (enabled) {
        if (active_config.main_stream.size.width <
                capabilities.lens_correction_min_width ||
            active_config.main_stream.size.height <
                capabilities.lens_correction_min_height) {
            return MakeConfigError("lens_correction.enabled",
                                   "main stream size unsupported", error);
        }
        if (active_config.sub_stream.enabled &&
            (active_config.sub_stream.size.width <
                 capabilities.lens_correction_min_width ||
             active_config.sub_stream.size.height <
                 capabilities.lens_correction_min_height)) {
            return MakeConfigError("lens_correction.enabled",
                                   "sub stream size unsupported", error);
        }
    }
    const ConfigCode range_result = ValidateNumericControls(
        lens_correction, "lens_correction",
        capabilities.lens_correction_ranges, error);
    if (range_result != ConfigCode::kOk) {
        return range_result;
    }
    const ConfigCode option_result = ValidateOptionControls(
        lens_correction, "lens_correction",
        capabilities.lens_correction_options, error);
    if (option_result != ConfigCode::kOk) {
        return option_result;
    }
    return ConfigCode::kOk;
}

ConfigCode ValidateStabilizationConfig(
    const Json &value,
    const ImageCapabilities &capabilities,
    const MediaPipelineConfig &active_config,
    ConfigError *error) {
    if (!value.contains("stabilization")) {
        return ConfigCode::kOk;
    }
    if (!value.at("stabilization").is_object()) {
        return MakeConfigError("stabilization",
                               "missing or invalid value", error);
    }
    const Json &stabilization = value.at("stabilization");
    bool enabled = false;
    if (!json_reader::ReadField(stabilization, "enabled", &enabled)) {
        return MakeConfigError("stabilization.enabled",
                               "missing or invalid value", error);
    }
    if (enabled && !capabilities.stabilization_supported) {
        return MakeConfigError("stabilization.enabled",
                               "unsupported value", error);
    }
    if (enabled) {
        if (active_config.main_stream.size.width <
                capabilities.stabilization_min_width ||
            active_config.main_stream.size.height <
                capabilities.stabilization_min_height) {
            return MakeConfigError("stabilization.enabled",
                                   "main stream size unsupported", error);
        }
        if (active_config.sub_stream.enabled &&
            (active_config.sub_stream.size.width <
                 capabilities.stabilization_min_width ||
             active_config.sub_stream.size.height <
                 capabilities.stabilization_min_height)) {
            return MakeConfigError("stabilization.enabled",
                                   "sub stream size unsupported", error);
        }
    }
    const ConfigCode range_result = ValidateNumericControls(
        stabilization, "stabilization", capabilities.stabilization_ranges,
        error);
    if (range_result != ConfigCode::kOk) {
        return range_result;
    }
    const ConfigCode option_result = ValidateOptionControls(
        stabilization, "stabilization", capabilities.stabilization_options,
        error);
    if (option_result != ConfigCode::kOk) {
        return option_result;
    }
    return ConfigCode::kOk;
}

constexpr char kFieldFallbackExposureTimeDivisor[] =
    "strategy.fallback_exposure_time_divisor";
constexpr char kFieldGainBase[] = "strategy.gain_base";
constexpr char kFieldIsoTierThresholds[] = "strategy.iso_tier_thresholds";
constexpr char kFieldLowNoiseDenoise3dMax[] =
    "strategy.low_noise_denoise_3d_max";
constexpr char kFieldTierStabilitySamples[] = "strategy.tier_stability_samples";
constexpr char kFieldStrategyEnabled[] = "strategy.enabled";

bool IsMonotonicOrEqual(const std::array<int64_t, 3> &values) {
    return values[0] <= values[1] && values[1] <= values[2];
}

}  // namespace

ConfigCode VerifyImageConfig(const Json &value,
                             const ImageCapabilities &capabilities,
                             const MediaPipelineConfig &active_config,
                             ConfigError *error) {
    if (!value.is_object()) {
        return MakeConfigError("", "invalid image config", error);
    }
    const struct {
        const char *name;
        const std::vector<NumericControlCapability> *ranges;
        const std::vector<OptionControlCapability> *options;
    } sections[] = {
        {"basic", &capabilities.basic, nullptr},
        {"exposure", &capabilities.exposure_ranges,
         &capabilities.exposure_options},
        {"white_balance", &capabilities.white_balance_ranges,
         &capabilities.white_balance_options},
        {"enhancement", &capabilities.enhancement_ranges,
         &capabilities.enhancement_options},
        {"backlight", &capabilities.backlight_ranges,
         &capabilities.backlight_options},
        {"color_mode", nullptr, &capabilities.color_mode_options},
    };

    for (const auto &section_spec : sections) {
        if (!value.contains(section_spec.name) ||
            !value.at(section_spec.name).is_object()) {
            return MakeConfigError("", "invalid image config", error);
        }
        const Json &section = value.at(section_spec.name);
        if (section_spec.ranges != nullptr) {
            const ConfigCode result = ValidateNumericControls(
                section, section_spec.name, *section_spec.ranges, error);
            if (result != ConfigCode::kOk) {
                return result;
            }
        }
        if (section_spec.options != nullptr) {
            const ConfigCode result = ValidateOptionControls(
                section, section_spec.name, *section_spec.options, error);
            if (result != ConfigCode::kOk) {
                return result;
            }
        }
    }

    if (!value.contains("orientation") ||
        !value.at("orientation").is_object()) {
        return MakeConfigError("", "invalid image config", error);
    }
    const Json &orientation = value.at("orientation");
    bool mirror = false;
    if (!json_reader::ReadField(orientation, "mirror", &mirror)) {
        return MakeConfigError("orientation.mirror",
                               "missing or invalid value", error);
    }
    if (mirror && !capabilities.mirror_supported) {
        return MakeConfigError("orientation.mirror", "unsupported value", error);
    }
    bool flip = false;
    if (!json_reader::ReadField(orientation, "flip", &flip)) {
        return MakeConfigError("orientation.flip",
                               "missing or invalid value", error);
    }
    if (flip && !capabilities.flip_supported) {
        return MakeConfigError("orientation.flip", "unsupported value", error);
    }

    if (value.contains("strategy") && value.at("strategy").is_object()) {
        const Json &strategy = value.at("strategy");
        if (strategy.contains("enabled")) {
            bool enabled = false;
            if (!json_reader::ReadField(strategy, "enabled", &enabled)) {
                return MakeConfigError(
                    kFieldStrategyEnabled, "missing or invalid value", error);
            }
        }
        const std::string mode = strategy.value("mode", "low_noise");
        if (mode != "balanced" && mode != "low_noise" && mode != "detail") {
            return MakeConfigError("strategy.mode", "unsupported value", error);
        }
        int64_t fallback_exposure_time_divisor = 0;
        if (strategy.contains("fallback_exposure_time_divisor") &&
            !json_reader::ReadField(strategy, "fallback_exposure_time_divisor",
                                   &fallback_exposure_time_divisor)) {
            return MakeConfigError(
                kFieldFallbackExposureTimeDivisor, "invalid value", error);
        }
        int64_t gain_base = 0;
        if (strategy.contains("gain_base") &&
            !json_reader::ReadField(strategy, "gain_base", &gain_base)) {
            return MakeConfigError(kFieldGainBase, "invalid value", error);
        }
        if (strategy.contains("fallback_exposure_time_divisor") &&
            fallback_exposure_time_divisor < 1) {
            return MakeConfigError(kFieldFallbackExposureTimeDivisor,
                                   "invalid value", error);
        }
        if (strategy.contains("gain_base") && gain_base < 1) {
            return MakeConfigError(kFieldGainBase, "invalid value", error);
        }
        std::array<int64_t, 3> thresholds = {0, 0, 0};
        auto iso_iter = strategy.find("iso_tier_thresholds");
        if (iso_iter != strategy.end()) {
            if (!iso_iter->is_array() || iso_iter->size() != 3) {
                return MakeConfigError(kFieldIsoTierThresholds,
                                       "invalid value", error);
            }
            for (size_t i = 0; i < 3; ++i) {
                if (!(*iso_iter)[i].is_number_integer() &&
                    !(*iso_iter)[i].is_number_unsigned()) {
                    return MakeConfigError(kFieldIsoTierThresholds,
                                           "invalid value", error);
                }
                const int64_t parsed = (*iso_iter)[i].is_number_unsigned()
                                          ? static_cast<int64_t>(
                                                (*iso_iter)[i].get<uint64_t>())
                                          : (*iso_iter)[i].get<int64_t>();
                if (parsed < 1 || parsed > 65535) {
                    return MakeConfigError(kFieldIsoTierThresholds,
                                           "invalid value", error);
                }
                thresholds[i] = parsed;
            }
            if (!IsMonotonicOrEqual(thresholds)) {
                return MakeConfigError(kFieldIsoTierThresholds,
                                       "invalid value", error);
            }
        }

        auto denoise_iter = strategy.find("low_noise_denoise_3d_max");
        if (denoise_iter != strategy.end()) {
            if (!denoise_iter->is_array() || denoise_iter->size() != 4) {
                return MakeConfigError(kFieldLowNoiseDenoise3dMax,
                                       "invalid value", error);
            }
            for (const Json &value : *denoise_iter) {
                if (!value.is_number_integer() &&
                    !value.is_number_unsigned()) {
                    return MakeConfigError(kFieldLowNoiseDenoise3dMax,
                                           "invalid value", error);
                }
                int64_t parsed = value.is_number_unsigned()
                                     ? static_cast<int64_t>(value.get<uint64_t>())
                                     : value.get<int64_t>();
                if (parsed < 0 || parsed > 100) {
                    return MakeConfigError(kFieldLowNoiseDenoise3dMax,
                                           "invalid value", error);
                }
            }
        }
        int32_t tier_stability_samples = 0;
        if (strategy.contains("tier_stability_samples") &&
            !json_reader::ReadField(strategy, "tier_stability_samples",
                                   &tier_stability_samples)) {
            return MakeConfigError(kFieldTierStabilitySamples,
                                   "invalid value", error);
        }
        if (strategy.contains("tier_stability_samples") &&
            (tier_stability_samples < 1 || tier_stability_samples > 10)) {
            return MakeConfigError(kFieldTierStabilitySamples,
                                   "invalid value", error);
        }
    }
    ConfigCode result =
        ValidateLensCorrectionConfig(value, capabilities, active_config, error);
    if (result != ConfigCode::kOk) {
        return result;
    }
    return ValidateStabilizationConfig(value, capabilities, active_config,
                                       error);
}

}  // namespace media_internal
}  // namespace live_stream
