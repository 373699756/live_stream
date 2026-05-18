#include "media_config_codec.h"

#include "live_stream/json_utils.h"
#include "media_pipeline.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace live_stream {

void to_json(ConfigJson &json, const VideoSize &size);
void from_json(const ConfigJson &json, VideoSize &size);
void to_json(ConfigJson &json, const VideoCodec &codec);
void from_json(const ConfigJson &json, VideoCodec &codec);
void to_json(ConfigJson &json, const RateControlMode &mode);
void from_json(const ConfigJson &json, RateControlMode &mode);
void to_json(ConfigJson &json, const GopMode &mode);
void from_json(const ConfigJson &json, GopMode &mode);

namespace detail {

bool ParseResolutionText(const std::string &text, VideoSize *size) {
    if (size == nullptr) {
        return false;
    }
    const std::string::size_type split = text.find('x');
    if (split == std::string::npos || split == 0 || split + 1 >= text.size()) {
        return false;
    }
    auto parse_part = [](const std::string &part, uint32_t *value) {
        if (value == nullptr || part.empty()) {
            return false;
        }
        uint64_t parsed = 0;
        for (char ch : part) {
            if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
                return false;
            }
            parsed = parsed * 10 + static_cast<uint64_t>(ch - '0');
            if (parsed > std::numeric_limits<uint32_t>::max()) {
                return false;
            }
        }
        if (parsed == 0) {
            return false;
        }
        *value = static_cast<uint32_t>(parsed);
        return true;
    };

    uint32_t width = 0;
    uint32_t height = 0;
    if (!parse_part(text.substr(0, split), &width) ||
        !parse_part(text.substr(split + 1), &height)) {
        return false;
    }
    size->width = width;
    size->height = height;
    return true;
}

bool ParseCodecText(const std::string &codec, VideoCodec *parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (codec == "h264") {
        *parsed = VideoCodec::kH264;
        return true;
    }
    if (codec == "h265") {
        *parsed = VideoCodec::kH265;
        return true;
    }
    if (codec == "jpeg") {
        *parsed = VideoCodec::kJpeg;
        return true;
    }
    if (codec == "mjpeg") {
        *parsed = VideoCodec::kMjpeg;
        return true;
    }
    return false;
}

const char *CodecToString(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::kH264:
            return "h264";
        case VideoCodec::kH265:
            return "h265";
        case VideoCodec::kJpeg:
            return "jpeg";
        case VideoCodec::kMjpeg:
            return "mjpeg";
    }
    return "h264";
}

bool ParseRateControlText(const std::string &rc_mode,
                          RateControlMode *parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (rc_mode == "cbr") {
        *parsed = RateControlMode::kCbr;
        return true;
    }
    if (rc_mode == "vbr") {
        *parsed = RateControlMode::kVbr;
        return true;
    }
    if (rc_mode == "fixqp" || rc_mode == "fix_qp") {
        *parsed = RateControlMode::kFixQp;
        return true;
    }
    return false;
}

const char *RateControlToString(RateControlMode mode) {
    switch (mode) {
        case RateControlMode::kCbr:
            return "cbr";
        case RateControlMode::kVbr:
            return "vbr";
        case RateControlMode::kFixQp:
            return "fixqp";
    }
    return "cbr";
}

bool ParseGopModeText(const std::string &gop_mode, GopMode *parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (gop_mode == "normal_p") {
        *parsed = GopMode::kNormalP;
        return true;
    }
    if (gop_mode == "dual_p") {
        *parsed = GopMode::kDualP;
        return true;
    }
    if (gop_mode == "smart_p") {
        *parsed = GopMode::kSmartP;
        return true;
    }
    return false;
}

const char *GopModeToString(GopMode mode) {
    switch (mode) {
        case GopMode::kNormalP:
            return "normal_p";
        case GopMode::kDualP:
            return "dual_p";
        case GopMode::kSmartP:
            return "smart_p";
    }
    return "normal_p";
}

}  // namespace detail

void to_json(ConfigJson &json, const VideoSize &size) {
    json = std::to_string(size.width) + "x" + std::to_string(size.height);
}

void from_json(const ConfigJson &json, VideoSize &size) {
    std::string text;
    json.get_to(text);

    VideoSize parsed;
    if (detail::ParseResolutionText(text, &parsed)) {
        size = parsed;
    }
}

void to_json(ConfigJson &json, const VideoCodec &codec) {
    json = detail::CodecToString(codec);
}

void from_json(const ConfigJson &json, VideoCodec &codec) {
    std::string text;
    json.get_to(text);
    (void)detail::ParseCodecText(text, &codec);
}

void to_json(ConfigJson &json, const RateControlMode &mode) {
    json = detail::RateControlToString(mode);
}

void from_json(const ConfigJson &json, RateControlMode &mode) {
    std::string text;
    json.get_to(text);
    (void)detail::ParseRateControlText(text, &mode);
}

void to_json(ConfigJson &json, const GopMode &mode) {
    json = detail::GopModeToString(mode);
}

void from_json(const ConfigJson &json, GopMode &mode) {
    std::string text;
    json.get_to(text);
    (void)detail::ParseGopModeText(text, &mode);
}

namespace media_internal {

using detail::ParseCodecText;
using detail::ParseGopModeText;
using detail::ParseRateControlText;
using detail::ParseResolutionText;

constexpr uint32_t kDefaultSensorFrameRate = 30;

ConfigResult ValidateVideoStreamJson(const ConfigJson &stream,
                                     const std::string &prefix) {
    if (!stream.is_object()) {
        return ConfigResult::Failure(prefix, "missing object");
    }

    // Validate enabled (bool)
    const ConfigJson *enabled_field = json_utils::FindField(stream, "enabled");
    if (enabled_field == nullptr || !enabled_field->is_boolean()) {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "enabled"),
                                     "missing or invalid value");
    }

    // Validate codec (string, must be valid codec)
    const ConfigJson *codec_field = json_utils::FindField(stream, "codec");
    if (codec_field != nullptr && codec_field->is_string()) {
        std::string codec_text;
        codec_field->get_to(codec_text);
        VideoCodec dummy = VideoCodec::kH264;
        if (!ParseCodecText(codec_text, &dummy)) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "codec"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "codec"),
                                     "missing or invalid value");
    }

    // Validate resolution (string, must be valid resolution)
    const ConfigJson *resolution_field = json_utils::FindField(stream, "resolution");
    if (resolution_field != nullptr && resolution_field->is_string()) {
        std::string resolution_text;
        resolution_field->get_to(resolution_text);
        VideoSize dummy_size;
        if (!ParseResolutionText(resolution_text, &dummy_size)) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "resolution"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "resolution"),
                                     "missing or invalid value");
    }

    // Validate fps (uint32)
    const ConfigJson *fps_field = json_utils::FindField(stream, "fps");
    if (fps_field != nullptr) {
        uint64_t fps_value = 0;
        if (fps_field->is_number_unsigned()) {
            fps_value = fps_field->get<uint64_t>();
        } else if (fps_field->is_number_integer()) {
            const int64_t signed_fps = fps_field->get<int64_t>();
            if (signed_fps < 0 || signed_fps > std::numeric_limits<uint32_t>::max()) {
                return ConfigResult::Failure(json_utils::JoinField(prefix, "fps"),
                                             "missing or invalid value");
            }
            fps_value = static_cast<uint64_t>(signed_fps);
        } else {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "fps"),
                                         "missing or invalid value");
        }
        if (fps_value > std::numeric_limits<uint32_t>::max()) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "fps"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "fps"),
                                     "missing or invalid value");
    }

    // Validate bitrate_kbps (uint32)
    const ConfigJson *bitrate_field = json_utils::FindField(stream, "bitrate_kbps");
    if (bitrate_field != nullptr) {
        uint64_t bitrate_value = 0;
        if (bitrate_field->is_number_unsigned()) {
            bitrate_value = bitrate_field->get<uint64_t>();
        } else if (bitrate_field->is_number_integer()) {
            const int64_t signed_bitrate = bitrate_field->get<int64_t>();
            if (signed_bitrate < 0 || signed_bitrate > std::numeric_limits<uint32_t>::max()) {
                return ConfigResult::Failure(json_utils::JoinField(prefix, "bitrate_kbps"),
                                             "missing or invalid value");
            }
            bitrate_value = static_cast<uint64_t>(signed_bitrate);
        } else {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "bitrate_kbps"),
                                         "missing or invalid value");
        }
        if (bitrate_value > std::numeric_limits<uint32_t>::max()) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "bitrate_kbps"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "bitrate_kbps"),
                                     "missing or invalid value");
    }

    // Validate rate_control (string, must be valid rate control mode)
    const ConfigJson *rc_field = json_utils::FindField(stream, "rate_control");
    if (rc_field != nullptr && rc_field->is_string()) {
        std::string rc_text;
        rc_field->get_to(rc_text);
        RateControlMode dummy_mode = RateControlMode::kCbr;
        if (!ParseRateControlText(rc_text, &dummy_mode)) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "rate_control"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "rate_control"),
                                     "missing or invalid value");
    }

    // Validate gop (uint32)
    const ConfigJson *gop_field = json_utils::FindField(stream, "gop");
    if (gop_field != nullptr) {
        uint64_t gop_value = 0;
        if (gop_field->is_number_unsigned()) {
            gop_value = gop_field->get<uint64_t>();
        } else if (gop_field->is_number_integer()) {
            const int64_t signed_gop = gop_field->get<int64_t>();
            if (signed_gop < 0 || signed_gop > std::numeric_limits<uint32_t>::max()) {
                return ConfigResult::Failure(json_utils::JoinField(prefix, "gop"),
                                             "missing or invalid value");
            }
            gop_value = static_cast<uint64_t>(signed_gop);
        } else {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "gop"),
                                         "missing or invalid value");
        }
        if (gop_value > std::numeric_limits<uint32_t>::max()) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "gop"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "gop"),
                                     "missing or invalid value");
    }

    // Validate gop_mode (string, must be valid gop mode)
    const ConfigJson *gop_mode_field = json_utils::FindField(stream, "gop_mode");
    if (gop_mode_field != nullptr && gop_mode_field->is_string()) {
        std::string gop_mode_text;
        gop_mode_field->get_to(gop_mode_text);
        GopMode dummy_gop_mode = GopMode::kNormalP;
        if (!ParseGopModeText(gop_mode_text, &dummy_gop_mode)) {
            return ConfigResult::Failure(json_utils::JoinField(prefix, "gop_mode"),
                                         "missing or invalid value");
        }
    } else {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "gop_mode"),
                                     "missing or invalid value");
    }

    const ConfigJson *smart_codec = json_utils::FindField(stream, "smart_codec");
    if (smart_codec != nullptr && !smart_codec->is_boolean()) {
        return ConfigResult::Failure(json_utils::JoinField(prefix, "smart_codec"),
                                     "missing or invalid value");
    }
    return ConfigResult::Success();
}

const VideoStreamCapabilities *
FindStreamCapabilities(const MediaCapabilities &capabilities,
                       StreamId stream_id) {
    for (const VideoStreamCapabilities &stream : capabilities.streams) {
        if (stream.stream_id == stream_id) {
            return &stream;
        }
    }
    return nullptr;
}

const CodecCapability *
FindCodecCapability(const VideoStreamCapabilities &capabilities,
                    VideoCodec codec) {
    for (const CodecCapability &item : capabilities.codecs) {
        if (item.codec == codec) {
            return &item;
        }
    }
    return nullptr;
}

bool ContainsResolution(const VideoStreamCapabilities &capabilities,
                        const VideoSize &size) {
    for (const VideoResolution &item : capabilities.resolutions) {
        if (item.width == size.width && item.height == size.height) {
            return true;
        }
    }
    return false;
}

bool ContainsRateControl(const VideoStreamCapabilities &capabilities,
                         RateControlMode mode) {
    for (RateControlMode item : capabilities.rate_control_modes) {
        if (item == mode) {
            return true;
        }
    }
    return false;
}

bool ContainsString(const std::vector<std::string> &values,
                    const std::string &value) {
    for (const std::string &item : values) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

ConfigResult
ValidateNumericControls(const ConfigJson &section,
                        const std::string &section_name,
                        const std::vector<NumericControlCapability> &controls) {
    for (const NumericControlCapability &control : controls) {
        int64_t value = 0;
        const std::string field = json_utils::JoinField(section_name, control.name);
        if (!json_utils::Load(section, control.name.c_str(), &value, control.min,
                              control.max)) {
            return ConfigResult::Failure(field, "missing or unsupported value");
        }
    }
    return ConfigResult::Success();
}

ConfigResult
ValidateOptionControls(const ConfigJson &section,
                       const std::string &section_name,
                       const std::vector<OptionControlCapability> &controls) {
    for (const OptionControlCapability &control : controls) {
        const std::string field = json_utils::JoinField(section_name, control.name);
        std::string value;
        if (json_utils::Load(section, control.name.c_str(), &value)) {
            if (!ContainsString(control.values, value)) {
                return ConfigResult::Failure(field, "unsupported value");
            }
            continue;
        }
        bool enabled = false;
        if (!json_utils::Load(section, control.name.c_str(), &enabled)) {
            return ConfigResult::Failure(field, "missing or invalid value");
        }
        if (!ContainsString(control.values, enabled ? "true" : "false")) {
            return ConfigResult::Failure(field, "unsupported value");
        }
    }
    return ConfigResult::Success();
}

ConfigResult ValidateVideoStreamConfig(
    const VideoConfig::Stream &stream,
    const VideoStreamCapabilities &stream_capabilities,
    const std::string &stream_prefix) {
    const CodecCapability *codec_capability =
        FindCodecCapability(stream_capabilities, stream.codec);
    if (codec_capability == nullptr) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "codec"),
                                     "unsupported value");
    }
    if (!ContainsResolution(stream_capabilities, stream.resolution)) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "resolution"),
                                     "unsupported value");
    }
    if (stream.fps < stream_capabilities.frame_rate.min_fps ||
        stream.fps > stream_capabilities.frame_rate.max_fps) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "fps"),
                                     "unsupported value");
    }
    if (stream.bitrate_kbps < stream_capabilities.bitrate.min_kbps ||
        stream.bitrate_kbps > stream_capabilities.bitrate.max_kbps) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "bitrate_kbps"),
                                     "unsupported value");
    }
    if (!ContainsRateControl(stream_capabilities, stream.rate_control)) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "rate_control"),
                                     "unsupported value");
    }
    if (stream.gop < stream_capabilities.gop.min ||
        stream.gop > stream_capabilities.gop.max) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "gop"),
                                     "unsupported value");
    }
    if (stream.smart_codec && !stream_capabilities.smart_codec_supported) {
        return ConfigResult::Failure(json_utils::JoinField(stream_prefix, "smart_codec"),
                                     "unsupported value");
    }
    return ConfigResult::Success();
}

void ApplyStreamConfig(StreamId stream_id,
                       const VideoConfig::Stream &source,
                       VideoStreamConfig *target) {
    if (target == nullptr) {
        return;
    }
    target->stream_id = stream_id;
    target->enabled = source.enabled;
    target->codec = source.codec;
    target->size = source.resolution;
    target->frame_rate.source_fps = static_cast<int32_t>(kDefaultSensorFrameRate);
    target->frame_rate.target_fps = static_cast<int32_t>(source.fps);
    target->bitrate_kbps = source.bitrate_kbps;
    target->gop = source.gop;
    target->rc_mode = source.rate_control;
    target->gop_mode = source.gop_mode;
}

void to_json(ConfigJson &json, const VideoConfig::Stream &stream) {
    json = ConfigJson::object();
    json["enabled"] = stream.enabled;
    json["codec"] = stream.codec;
    json["resolution"] = stream.resolution;
    json["fps"] = stream.fps;
    json["bitrate_kbps"] = stream.bitrate_kbps;
    json["rate_control"] = stream.rate_control;
    json["gop"] = stream.gop;
    json["gop_mode"] = stream.gop_mode;
    json["smart_codec"] = stream.smart_codec;
}

void from_json(const ConfigJson &json, VideoConfig::Stream &stream) {
    json.at("enabled").get_to(stream.enabled);
    json.at("codec").get_to(stream.codec);
    json.at("resolution").get_to(stream.resolution);
    json.at("fps").get_to(stream.fps);
    json.at("bitrate_kbps").get_to(stream.bitrate_kbps);
    json.at("rate_control").get_to(stream.rate_control);
    json.at("gop").get_to(stream.gop);
    json.at("gop_mode").get_to(stream.gop_mode);

    const auto smart_codec = json.find("smart_codec");
    if (smart_codec != json.end()) {
        smart_codec->get_to(stream.smart_codec);
    }
}

void to_json(ConfigJson &json, const VideoConfig &config) {
    json = ConfigJson::object();
    json["streams"] = ConfigJson::object();
    json["streams"]["main"] = config.main;
    json["streams"]["sub"] = config.sub;
}

void from_json(const ConfigJson &json, VideoConfig &config) {
    json.at("streams").at("main").get_to(config.main);
    json.at("streams").at("sub").get_to(config.sub);
}

ConfigResult DecodeVideoConfig(const ConfigJson &value, VideoConfig *config) {
    if (config == nullptr) {
        return ConfigResult::Failure("", "invalid video config target");
    }
    if (!value.is_object()) {
        return ConfigResult::Failure("", "invalid video config");
    }

    const ConfigJson *streams = nullptr;
    if (!json_utils::LoadObject(value, "streams", &streams)) {
        return ConfigResult::Failure("streams", "missing object");
    }

    const ConfigJson *main_stream = nullptr;
    if (!json_utils::LoadObject(*streams, "main", &main_stream)) {
        return ConfigResult::Failure("streams.main", "missing object");
    }
    ConfigResult result = ValidateVideoStreamJson(*main_stream, "streams.main");
    if (!result.ok) {
        return result;
    }

    const ConfigJson *sub_stream = nullptr;
    if (!json_utils::LoadObject(*streams, "sub", &sub_stream)) {
        return ConfigResult::Failure("streams.sub", "missing object");
    }
    result = ValidateVideoStreamJson(*sub_stream, "streams.sub");
    if (!result.ok) {
        return result;
    }

    value.get_to(*config);
    return ConfigResult::Success();
}

ConfigResult ValidateVideoConfig(const VideoConfig &config,
                                 const MediaCapabilities &capabilities) {
    if (capabilities.streams.empty()) {
        return ConfigResult::Failure("", "media capabilities unavailable");
    }

    const struct {
        const char *name;
        StreamId stream_id;
        const VideoConfig::Stream *stream;
    } stream_specs[] = {
        {"main", StreamId::kMain, &config.main},
        {"sub", StreamId::kSub, &config.sub},
    };

    for (const auto &stream_spec : stream_specs) {
        const std::string stream_prefix = json_utils::JoinField("streams", stream_spec.name);
        const VideoStreamCapabilities *stream_capabilities =
            FindStreamCapabilities(capabilities, stream_spec.stream_id);
        if (stream_capabilities == nullptr) {
            return ConfigResult::Failure(stream_prefix, "missing capabilities");
        }

        const ConfigResult result = ValidateVideoStreamConfig(
            *stream_spec.stream, *stream_capabilities, stream_prefix);
        if (!result.ok) {
            return result;
        }
    }
    return ConfigResult::Success();
}

ConfigResult BuildPipelineConfig(const VideoConfig &config,
                                 const MediaPipelineConfig &fallback,
                                 MediaPipelineConfig *pipeline_config) {
    if (pipeline_config == nullptr) {
        return ConfigResult::Failure("", "invalid video config target");
    }
    MediaPipelineConfig next_config = fallback;
    ApplyStreamConfig(StreamId::kMain, config.main, &next_config.main_stream);
    ApplyStreamConfig(StreamId::kSub, config.sub, &next_config.sub_stream);
    *pipeline_config = next_config;
    return ConfigResult::Success();
}

ConfigResult ValidateImageConfig(const ConfigJson &value,
                                 const ImageCapabilities &capabilities) {
    if (!value.is_object()) {
        return ConfigResult::Failure("", "invalid image config");
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
        const ConfigJson *section = nullptr;
        if (!json_utils::LoadObject(value, section_spec.name, &section)) {
            return ConfigResult::Failure(section_spec.name, "missing object");
        }
        if (section_spec.ranges != nullptr) {
            const ConfigResult result = ValidateNumericControls(
                *section, section_spec.name, *section_spec.ranges);
            if (!result.ok) {
                return result;
            }
        }
        if (section_spec.options != nullptr) {
            const ConfigResult result = ValidateOptionControls(
                *section, section_spec.name, *section_spec.options);
            if (!result.ok) {
                return result;
            }
        }
    }

    const ConfigJson *orientation = nullptr;
    if (!json_utils::LoadObject(value, "orientation", &orientation)) {
        return ConfigResult::Failure("orientation", "missing object");
    }
    bool mirror = false;
    if (!json_utils::Load(*orientation, "mirror", &mirror)) {
        return ConfigResult::Failure("orientation.mirror",
                                     "missing or invalid value");
    }
    if (mirror && !capabilities.mirror_supported) {
        return ConfigResult::Failure("orientation.mirror", "unsupported value");
    }
    bool flip = false;
    if (!json_utils::Load(*orientation, "flip", &flip)) {
        return ConfigResult::Failure("orientation.flip",
                                     "missing or invalid value");
    }
    if (flip && !capabilities.flip_supported) {
        return ConfigResult::Failure("orientation.flip", "unsupported value");
    }
    return ConfigResult::Success();
}

ConfigResult ParseVideoConfig(const ConfigJson &value,
                              const MediaPipelineConfig &fallback,
                              const MediaCapabilities &capabilities,
                              MediaPipelineConfig *parsed) {
    VideoConfig config;
    ConfigResult result = DecodeVideoConfig(value, &config);
    if (!result.ok) {
        return result;
    }
    result = ValidateVideoConfig(config, capabilities);
    if (!result.ok) {
        return result;
    }
    result = BuildPipelineConfig(config, fallback, parsed);
    if (!result.ok) {
        return result;
    }
    if (!IsValidMediaPipelineConfig(*parsed)) {
        return ConfigResult::Failure("streams.main",
                                     "invalid media pipeline config");
    }
    return ConfigResult::Success();
}

}  // namespace media_internal
}  // namespace live_stream
