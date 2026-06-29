#include "media_config_codec.h"

#include "config_error.h"
#include "json_reader.h"

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
        const std::string mode = strategy.value("mode", "low_noise");
        if (mode != "balanced" && mode != "low_noise" && mode != "detail") {
            return MakeConfigError("strategy.mode", "unsupported value", error);
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
