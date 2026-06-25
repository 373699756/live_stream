#include "media_config_codec.h"

#include "json_reader.h"
#include "media_pipeline.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace live_stream {

void to_json(Json &json, const VideoSize &size);
void from_json(const Json &json, VideoSize &size);
void to_json(Json &json, const Codec &codec);
void from_json(const Json &json, Codec &codec);
void to_json(Json &json, const RateControlMode &mode);
void from_json(const Json &json, RateControlMode &mode);
void to_json(Json &json, const GopMode &mode);
void from_json(const Json &json, GopMode &mode);

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

bool ParseCodecText(const std::string &codec, Codec *parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (codec == "h264") {
        *parsed = Codec::kH264;
        return true;
    }
    if (codec == "h265") {
        *parsed = Codec::kH265;
        return true;
    }
    if (codec == "jpeg") {
        *parsed = Codec::kJpeg;
        return true;
    }
    if (codec == "mjpeg") {
        *parsed = Codec::kMjpeg;
        return true;
    }
    return false;
}

const char *CodecToString(Codec codec) {
    switch (codec) {
        case Codec::kH264:
            return "h264";
        case Codec::kH265:
            return "h265";
        case Codec::kJpeg:
            return "jpeg";
        case Codec::kMjpeg:
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

void to_json(Json &json, const VideoSize &size) {
    json = std::to_string(size.width) + "x" + std::to_string(size.height);
}

void from_json(const Json &json, VideoSize &size) {
    std::string text;
    json.get_to(text);

    VideoSize parsed;
    if (detail::ParseResolutionText(text, &parsed)) {
        size = parsed;
    }
}

void to_json(Json &json, const Codec &codec) {
    json = detail::CodecToString(codec);
}

void from_json(const Json &json, Codec &codec) {
    std::string text;
    json.get_to(text);
    (void)detail::ParseCodecText(text, &codec);
}

void to_json(Json &json, const RateControlMode &mode) {
    json = detail::RateControlToString(mode);
}

void from_json(const Json &json, RateControlMode &mode) {
    std::string text;
    json.get_to(text);
    (void)detail::ParseRateControlText(text, &mode);
}

void to_json(Json &json, const GopMode &mode) {
    json = detail::GopModeToString(mode);
}

void from_json(const Json &json, GopMode &mode) {
    std::string text;
    json.get_to(text);
    (void)detail::ParseGopModeText(text, &mode);
}

void to_json(Json &json, const VideoRoiRegion &region) {
    json = Json::object();
    json["enabled"] = region.enabled;
    json["x"] = region.x;
    json["y"] = region.y;
    json["width"] = region.width;
    json["height"] = region.height;
    json["qp"] = region.qp;
    json["absolute_qp"] = region.absolute_qp;
}

void from_json(const Json &json, VideoRoiRegion &region) {
    json.at("enabled").get_to(region.enabled);
    json.at("x").get_to(region.x);
    json.at("y").get_to(region.y);
    json.at("width").get_to(region.width);
    json.at("height").get_to(region.height);
    json.at("qp").get_to(region.qp);
    json.at("absolute_qp").get_to(region.absolute_qp);
}

void to_json(Json &json, const VideoRoiConfig &roi) {
    json = Json::object();
    json["enabled"] = roi.enabled;
    json["regions"] = Json::array();
    for (const VideoRoiRegion &region : roi.regions) {
        json["regions"].push_back(region);
    }
}

void from_json(const Json &json, VideoRoiConfig &roi) {
    json.at("enabled").get_to(roi.enabled);
    roi.regions.clear();
    for (const Json &region : json.at("regions")) {
        roi.regions.push_back(region.get<VideoRoiRegion>());
    }
}

namespace media_internal {

using detail::ParseCodecText;
using detail::ParseGopModeText;
using detail::ParseRateControlText;
using detail::ParseResolutionText;

constexpr uint32_t kDefaultSensorFrameRate = 30;
constexpr uint32_t kMaxVideoRoiRegions = 8;
constexpr int32_t kMinVideoRoiQp = -51;
constexpr int32_t kMaxVideoRoiQp = 51;

ConfigCode RejectConfig(const std::string &field,
                          const std::string &reason,
                          ConfigError *error) {
    if (error != nullptr) {
        error->field = field;
        error->message = reason;
    }
    return ConfigCode::kVerify;
}

std::string JoinField(const std::string &parent, const char *child) {
    if (parent.empty()) {
        return child != nullptr ? child : "";
    }
    return child != nullptr ? (parent + "." + child) : parent;
}

std::string JoinField(const std::string &parent, const std::string &child) {
    return JoinField(parent, child.c_str());
}

bool ValidateVideoStreamJson(const Json &stream) {
    if (!stream.is_object()) {
        return false;
    }

    bool enabled = false;
    if (!json_reader::ReadField(stream, "enabled", &enabled)) {
        return false;
    }

    std::string codec_text;
    Codec dummy_codec = Codec::kH264;
    if (!json_reader::ReadField(stream, "codec", &codec_text) ||
        !ParseCodecText(codec_text, &dummy_codec)) {
        return false;
    }

    std::string resolution_text;
    VideoSize parsed_resolution;
    if (!json_reader::ReadField(stream, "resolution", &resolution_text) ||
        !ParseResolutionText(resolution_text, &parsed_resolution)) {
        return false;
    }

    uint32_t fps = 0;
    if (!json_reader::ReadField(stream, "fps", &fps)) {
        return false;
    }

    uint32_t bitrate_kbps = 0;
    if (!json_reader::ReadField(stream, "bitrate_kbps", &bitrate_kbps)) {
        return false;
    }

    std::string rc_text;
    RateControlMode dummy_mode = RateControlMode::kCbr;
    if (!json_reader::ReadField(stream, "rate_control", &rc_text) ||
        !ParseRateControlText(rc_text, &dummy_mode)) {
        return false;
    }

    uint32_t gop = 0;
    if (!json_reader::ReadField(stream, "gop", &gop)) {
        return false;
    }

    std::string gop_mode_text;
    GopMode dummy_gop_mode = GopMode::kNormalP;
    if (!json_reader::ReadField(stream, "gop_mode", &gop_mode_text) ||
        !ParseGopModeText(gop_mode_text, &dummy_gop_mode)) {
        return false;
    }

    bool smart_codec = false;
    if (stream.contains("smart_codec") &&
        !json_reader::ReadField(stream, "smart_codec", &smart_codec)) {
        return false;
    }
    if (stream.contains("roi")) {
        const Json &roi = stream.at("roi");
        if (!roi.is_object()) {
            return false;
        }
        bool roi_enabled = false;
        if (!json_reader::ReadField(roi, "enabled", &roi_enabled)) {
            return false;
        }
        if (!roi.contains("regions") || !roi.at("regions").is_array() ||
            roi.at("regions").size() > kMaxVideoRoiRegions) {
            return false;
        }
        for (const Json &region : roi.at("regions")) {
            if (!region.is_object()) {
                return false;
            }
            bool region_enabled = false;
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            int32_t qp = 0;
            bool absolute_qp = false;
            if (!json_reader::ReadField(region, "enabled",
                                       &region_enabled) ||
                !json_reader::ReadField(region, "x", &x) ||
                !json_reader::ReadField(region, "y", &y) ||
                !json_reader::ReadField(region, "width", &width) ||
                !json_reader::ReadField(region, "height", &height) ||
                !json_reader::ReadField(region, "qp", &qp,
                                       kMinVideoRoiQp, kMaxVideoRoiQp) ||
                !json_reader::ReadField(region, "absolute_qp",
                                       &absolute_qp)) {
                return false;
            }
        }
    }
    return true;
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
                    Codec codec) {
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

ConfigCode
ValidateNumericControls(const Json &section,
                        const std::string &section_name,
                        const std::vector<NumericControlCapability> &controls,
                        ConfigError *error) {
    for (const NumericControlCapability &control : controls) {
        int64_t value = 0;
        const std::string field = JoinField(section_name, control.name);
        if (!json_reader::ReadField(section, control.name.c_str(), &value, control.min,
                                   control.max)) {
            return RejectConfig(field, "missing or unsupported value", error);
        }
    }
    return ConfigCode::kOk;
}

ConfigCode
ValidateOptionControls(const Json &section,
                       const std::string &section_name,
                       const std::vector<OptionControlCapability> &controls,
                       ConfigError *error) {
    for (const OptionControlCapability &control : controls) {
        const std::string field = JoinField(section_name, control.name);
        std::string value;
        if (json_reader::ReadField(section, control.name.c_str(), &value)) {
            if (!ContainsString(control.values, value)) {
                return RejectConfig(field, "unsupported value", error);
            }
            continue;
        }
        bool enabled = false;
        if (!json_reader::ReadField(section, control.name.c_str(), &enabled)) {
            return RejectConfig(field, "missing or invalid value", error);
        }
        if (!ContainsString(control.values, enabled ? "true" : "false")) {
            return RejectConfig(field, "unsupported value", error);
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
        return RejectConfig("lens_correction",
                                     "missing or invalid value", error);
    }
    const Json &lens_correction = value.at("lens_correction");
    bool enabled = false;
    if (!json_reader::ReadField(lens_correction, "enabled", &enabled)) {
        return RejectConfig("lens_correction.enabled",
                                     "missing or invalid value", error);
    }
    if (enabled && !capabilities.lens_correction_supported) {
        return RejectConfig("lens_correction.enabled",
                                     "unsupported value", error);
    }
    if (enabled) {
        if (active_config.main_stream.size.width <
                capabilities.lens_correction_min_width ||
            active_config.main_stream.size.height <
                capabilities.lens_correction_min_height) {
            return RejectConfig("lens_correction.enabled",
                                         "main stream size unsupported", error);
        }
        if (active_config.sub_stream.enabled &&
            (active_config.sub_stream.size.width <
                 capabilities.lens_correction_min_width ||
             active_config.sub_stream.size.height <
                 capabilities.lens_correction_min_height)) {
            return RejectConfig("lens_correction.enabled",
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
        return RejectConfig("stabilization",
                                     "missing or invalid value", error);
    }
    const Json &stabilization = value.at("stabilization");
    bool enabled = false;
    if (!json_reader::ReadField(stabilization, "enabled", &enabled)) {
        return RejectConfig("stabilization.enabled",
                                     "missing or invalid value", error);
    }
    if (enabled && !capabilities.stabilization_supported) {
        return RejectConfig("stabilization.enabled",
                                     "unsupported value", error);
    }
    if (enabled) {
        if (active_config.main_stream.size.width <
                capabilities.stabilization_min_width ||
            active_config.main_stream.size.height <
                capabilities.stabilization_min_height) {
            return RejectConfig("stabilization.enabled",
                                         "main stream size unsupported", error);
        }
        if (active_config.sub_stream.enabled &&
            (active_config.sub_stream.size.width <
                 capabilities.stabilization_min_width ||
             active_config.sub_stream.size.height <
                 capabilities.stabilization_min_height)) {
            return RejectConfig("stabilization.enabled",
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

ConfigCode ValidateVideoStreamConfig(
    const VideoConfig::Stream &stream,
    const VideoStreamCapabilities &stream_capabilities,
    const std::string &stream_prefix,
    ConfigError *error) {
    const CodecCapability *codec_capability =
        FindCodecCapability(stream_capabilities, stream.codec);
    if (codec_capability == nullptr) {
        return RejectConfig(JoinField(stream_prefix, "codec"),
                                     "unsupported value", error);
    }
    if (!ContainsResolution(stream_capabilities, stream.resolution)) {
        return RejectConfig(JoinField(stream_prefix, "resolution"),
                                     "unsupported value", error);
    }
    if (stream.fps < stream_capabilities.frame_rate.min_fps ||
        stream.fps > stream_capabilities.frame_rate.max_fps) {
        return RejectConfig(JoinField(stream_prefix, "fps"),
                                     "unsupported value", error);
    }
    if (stream.bitrate_kbps < stream_capabilities.bitrate.min_kbps ||
        stream.bitrate_kbps > stream_capabilities.bitrate.max_kbps) {
        return RejectConfig(JoinField(stream_prefix, "bitrate_kbps"),
                                     "unsupported value", error);
    }
    if (!ContainsRateControl(stream_capabilities, stream.rate_control)) {
        return RejectConfig(JoinField(stream_prefix, "rate_control"),
                                     "unsupported value", error);
    }
    if (stream.gop < stream_capabilities.gop.min ||
        stream.gop > stream_capabilities.gop.max) {
        return RejectConfig(JoinField(stream_prefix, "gop"),
                                     "unsupported value", error);
    }
    if (stream.smart_codec && !stream_capabilities.smart_codec_supported) {
        return RejectConfig(JoinField(stream_prefix, "smart_codec"),
                                     "unsupported value", error);
    }
    if (stream.gop_mode == GopMode::kSmartP &&
        !stream_capabilities.smart_codec_supported) {
        return RejectConfig(JoinField(stream_prefix, "gop_mode"),
                                     "unsupported value", error);
    }
    if ((stream.smart_codec || stream.gop_mode == GopMode::kSmartP) &&
        stream.codec != Codec::kH264 && stream.codec != Codec::kH265) {
        return RejectConfig(JoinField(stream_prefix, "smart_codec"),
                                     "unsupported codec", error);
    }
    if (stream.roi.enabled || !stream.roi.regions.empty()) {
        if (!stream_capabilities.roi_supported ||
            stream_capabilities.max_roi_regions == 0) {
            return RejectConfig(JoinField(stream_prefix, "roi"),
                                         "unsupported value", error);
        }
        if (stream.codec != Codec::kH264 &&
            stream.codec != Codec::kH265) {
            return RejectConfig(JoinField(stream_prefix, "roi"),
                                         "unsupported codec", error);
        }
        const uint32_t max_roi_regions =
            stream_capabilities.max_roi_regions < kMaxVideoRoiRegions
                ? stream_capabilities.max_roi_regions
                : kMaxVideoRoiRegions;
        if (stream.roi.regions.size() > max_roi_regions) {
            return RejectConfig(JoinField(stream_prefix, "roi"),
                                         "too many regions", error);
        }
        for (size_t i = 0; i < stream.roi.regions.size(); ++i) {
            const VideoRoiRegion &region = stream.roi.regions[i];
            const std::string region_prefix =
                JoinField(JoinField(JoinField(stream_prefix, "roi"),
                                    "regions"),
                          std::to_string(i));
            if (region.width == 0 || region.height == 0) {
                return RejectConfig(region_prefix,
                                             "invalid region size", error);
            }
            if (region.x >= stream.resolution.width ||
                region.y >= stream.resolution.height ||
                region.width > stream.resolution.width - region.x ||
                region.height > stream.resolution.height - region.y) {
                return RejectConfig(region_prefix,
                                             "region outside stream frame", error);
            }
            if (region.qp < kMinVideoRoiQp || region.qp > kMaxVideoRoiQp) {
                return RejectConfig(JoinField(region_prefix, "qp"),
                                             "unsupported value", error);
            }
        }
    }
    return ConfigCode::kOk;
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
    target->gop_mode = source.smart_codec ? GopMode::kSmartP
                                          : source.gop_mode;
    target->roi = source.roi;
}

void to_json(Json &json, const VideoConfig::Stream &stream) {
    json = Json::object();
    json["enabled"] = stream.enabled;
    json["codec"] = stream.codec;
    json["resolution"] = stream.resolution;
    json["fps"] = stream.fps;
    json["bitrate_kbps"] = stream.bitrate_kbps;
    json["rate_control"] = stream.rate_control;
    json["gop"] = stream.gop;
    json["gop_mode"] = stream.gop_mode;
    json["smart_codec"] = stream.smart_codec;
    json["roi"] = stream.roi;
}

void from_json(const Json &json, VideoConfig::Stream &stream) {
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
    const auto roi = json.find("roi");
    if (roi != json.end()) {
        roi->get_to(stream.roi);
    }
}

void to_json(Json &json, const VideoConfig &config) {
    json = Json::object();
    json["streams"] = Json::object();
    json["streams"]["main"] = config.main;
    json["streams"]["sub"] = config.sub;
}

void from_json(const Json &json, VideoConfig &config) {
    json.at("streams").at("main").get_to(config.main);
    json.at("streams").at("sub").get_to(config.sub);
}

ConfigCode DecodeVideoConfig(const Json &value,
                               VideoConfig *config,
                               ConfigError *error) {
    if (config == nullptr) {
        return RejectConfig("", "invalid video config", error);
    }
    if (!value.is_object()) {
        return RejectConfig("", "invalid video config", error);
    }

    if (!value.contains("streams") || !value.at("streams").is_object()) {
        return RejectConfig("", "invalid video config", error);
    }
    const Json &streams = value.at("streams");

    if (!streams.contains("main") || !streams.at("main").is_object()) {
        return RejectConfig("", "invalid video config", error);
    }
    const Json &main_stream = streams.at("main");
    if (!ValidateVideoStreamJson(main_stream)) {
        return RejectConfig("", "invalid video config", error);
    }

    if (!streams.contains("sub") || !streams.at("sub").is_object()) {
        return RejectConfig("", "invalid video config", error);
    }
    const Json &sub_stream = streams.at("sub");
    if (!ValidateVideoStreamJson(sub_stream)) {
        return RejectConfig("", "invalid video config", error);
    }

    value.get_to(*config);
    return ConfigCode::kOk;
}

ConfigCode VerifyVideoConfig(const VideoConfig &config,
                               const MediaCapabilities &capabilities,
                               ConfigError *error) {
    if (capabilities.streams.empty()) {
        return RejectConfig("", "media capabilities unavailable", error);
    }
    if (!config.main.enabled) {
        return RejectConfig("streams.main.enabled",
                                     "main stream must stay enabled", error);
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
        const std::string stream_prefix = JoinField("streams", stream_spec.name);
        const VideoStreamCapabilities *stream_capabilities =
            FindStreamCapabilities(capabilities, stream_spec.stream_id);
        if (stream_capabilities == nullptr) {
            return RejectConfig(stream_prefix, "missing capabilities", error);
        }

        const ConfigCode result = ValidateVideoStreamConfig(
            *stream_spec.stream, *stream_capabilities, stream_prefix, error);
        if (result != ConfigCode::kOk) {
            return result;
        }
    }
    return ConfigCode::kOk;
}

ConfigCode BuildPipelineConfig(const VideoConfig &config,
                                 const MediaPipelineConfig &fallback,
                                 MediaPipelineConfig *pipeline_config,
                                 ConfigError *error) {
    if (pipeline_config == nullptr) {
        return RejectConfig("", "invalid video config", error);
    }
    MediaPipelineConfig next_config = fallback;
    ApplyStreamConfig(StreamId::kMain, config.main, &next_config.main_stream);
    ApplyStreamConfig(StreamId::kSub, config.sub, &next_config.sub_stream);
    *pipeline_config = next_config;
    return ConfigCode::kOk;
}

ConfigCode VerifyImageConfig(const Json &value,
                               const ImageCapabilities &capabilities,
                               const MediaPipelineConfig &active_config,
                               ConfigError *error) {
    if (!value.is_object()) {
        return RejectConfig("", "invalid image config", error);
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
            return RejectConfig("", "invalid image config", error);
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
        return RejectConfig("", "invalid image config", error);
    }
    const Json &orientation = value.at("orientation");
    bool mirror = false;
    if (!json_reader::ReadField(orientation, "mirror", &mirror)) {
        return RejectConfig("orientation.mirror",
                                     "missing or invalid value", error);
    }
    if (mirror && !capabilities.mirror_supported) {
        return RejectConfig("orientation.mirror", "unsupported value", error);
    }
    bool flip = false;
    if (!json_reader::ReadField(orientation, "flip", &flip)) {
        return RejectConfig("orientation.flip",
                                     "missing or invalid value", error);
    }
    if (flip && !capabilities.flip_supported) {
        return RejectConfig("orientation.flip", "unsupported value", error);
    }

    if (value.contains("strategy") && value.at("strategy").is_object()) {
        const Json &strategy = value.at("strategy");
        const std::string mode = strategy.value("mode", "low_noise");
        if (mode != "balanced" && mode != "low_noise" && mode != "detail") {
            return RejectConfig("strategy.mode", "unsupported value", error);
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

ConfigCode ParseVideoConfig(const Json &value,
                              const MediaPipelineConfig &fallback,
                              const MediaCapabilities &capabilities,
                              MediaPipelineConfig *parsed,
                              ConfigError *error) {
    VideoConfig config;
    ConfigCode result = DecodeVideoConfig(value, &config, error);
    if (result != ConfigCode::kOk) {
        return result;
    }
    result = VerifyVideoConfig(config, capabilities, error);
    if (result != ConfigCode::kOk) {
        return result;
    }
    result = BuildPipelineConfig(config, fallback, parsed, error);
    if (result != ConfigCode::kOk) {
        return result;
    }
    if (!IsValidMediaPipelineConfig(*parsed)) {
        return RejectConfig("streams.main",
                                     "invalid media pipeline config", error);
    }
    return ConfigCode::kOk;
}

}  // namespace media_internal
}  // namespace live_stream
