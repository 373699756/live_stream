#include "media_config_codec.h"

#include "json_utils.h"
#include "device_media_pipeline.h"

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

void to_json(ConfigJson &json, const VideoRoiRegion &region) {
    json = ConfigJson::object();
    json["enabled"] = region.enabled;
    json["x"] = region.x;
    json["y"] = region.y;
    json["width"] = region.width;
    json["height"] = region.height;
    json["qp"] = region.qp;
    json["absolute_qp"] = region.absolute_qp;
}

void from_json(const ConfigJson &json, VideoRoiRegion &region) {
    json.at("enabled").get_to(region.enabled);
    json.at("x").get_to(region.x);
    json.at("y").get_to(region.y);
    json.at("width").get_to(region.width);
    json.at("height").get_to(region.height);
    json.at("qp").get_to(region.qp);
    json.at("absolute_qp").get_to(region.absolute_qp);
}

void to_json(ConfigJson &json, const VideoRoiConfig &roi) {
    json = ConfigJson::object();
    json["enabled"] = roi.enabled;
    json["regions"] = ConfigJson::array();
    for (const VideoRoiRegion &region : roi.regions) {
        json["regions"].push_back(region);
    }
}

void from_json(const ConfigJson &json, VideoRoiConfig &roi) {
    json.at("enabled").get_to(roi.enabled);
    roi.regions.clear();
    for (const ConfigJson &region : json.at("regions")) {
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

std::string JoinField(const std::string &parent, const char *child) {
    if (parent.empty()) {
        return child != nullptr ? child : "";
    }
    return child != nullptr ? (parent + "." + child) : parent;
}

std::string JoinField(const std::string &parent, const std::string &child) {
    return JoinField(parent, child.c_str());
}

bool ValidateVideoStreamJson(const ConfigJson &stream) {
    if (!stream.is_object()) {
        return false;
    }

    bool enabled = false;
    if (!json_utils::ReadField(stream, "enabled", &enabled)) {
        return false;
    }

    std::string codec_text;
    VideoCodec dummy_codec = VideoCodec::kH264;
    if (!json_utils::ReadField(stream, "codec", &codec_text) ||
        !ParseCodecText(codec_text, &dummy_codec)) {
        return false;
    }

    std::string resolution_text;
    VideoSize dummy_size;
    if (!json_utils::ReadField(stream, "resolution", &resolution_text) ||
        !ParseResolutionText(resolution_text, &dummy_size)) {
        return false;
    }

    uint32_t fps = 0;
    if (!json_utils::ReadField(stream, "fps", &fps)) {
        return false;
    }

    uint32_t bitrate_kbps = 0;
    if (!json_utils::ReadField(stream, "bitrate_kbps", &bitrate_kbps)) {
        return false;
    }

    std::string rc_text;
    RateControlMode dummy_mode = RateControlMode::kCbr;
    if (!json_utils::ReadField(stream, "rate_control", &rc_text) ||
        !ParseRateControlText(rc_text, &dummy_mode)) {
        return false;
    }

    uint32_t gop = 0;
    if (!json_utils::ReadField(stream, "gop", &gop)) {
        return false;
    }

    std::string gop_mode_text;
    GopMode dummy_gop_mode = GopMode::kNormalP;
    if (!json_utils::ReadField(stream, "gop_mode", &gop_mode_text) ||
        !ParseGopModeText(gop_mode_text, &dummy_gop_mode)) {
        return false;
    }

    bool smart_codec = false;
    if (stream.contains("smart_codec") &&
        !json_utils::ReadField(stream, "smart_codec", &smart_codec)) {
        return false;
    }
    if (stream.contains("roi")) {
        const ConfigJson &roi = stream.at("roi");
        if (!roi.is_object()) {
            return false;
        }
        bool roi_enabled = false;
        if (!json_utils::ReadField(roi, "enabled", &roi_enabled)) {
            return false;
        }
        if (!roi.contains("regions") || !roi.at("regions").is_array() ||
            roi.at("regions").size() > kMaxVideoRoiRegions) {
            return false;
        }
        for (const ConfigJson &region : roi.at("regions")) {
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
            if (!json_utils::ReadField(region, "enabled",
                                       &region_enabled) ||
                !json_utils::ReadField(region, "x", &x) ||
                !json_utils::ReadField(region, "y", &y) ||
                !json_utils::ReadField(region, "width", &width) ||
                !json_utils::ReadField(region, "height", &height) ||
                !json_utils::ReadField(region, "qp", &qp,
                                       kMinVideoRoiQp, kMaxVideoRoiQp) ||
                !json_utils::ReadField(region, "absolute_qp",
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
        const std::string field = JoinField(section_name, control.name);
        if (!json_utils::ReadField(section, control.name.c_str(), &value, control.min,
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
        const std::string field = JoinField(section_name, control.name);
        std::string value;
        if (json_utils::ReadField(section, control.name.c_str(), &value)) {
            if (!ContainsString(control.values, value)) {
                return ConfigResult::Failure(field, "unsupported value");
            }
            continue;
        }
        bool enabled = false;
        if (!json_utils::ReadField(section, control.name.c_str(), &enabled)) {
            return ConfigResult::Failure(field, "missing or invalid value");
        }
        if (!ContainsString(control.values, enabled ? "true" : "false")) {
            return ConfigResult::Failure(field, "unsupported value");
        }
    }
    return ConfigResult::Success();
}

ConfigResult ValidateLensCorrectionConfig(
    const ConfigJson &value,
    const ImageCapabilities &capabilities,
    const MediaPipelineConfig &active_config) {
    if (!value.contains("lens_correction")) {
        return ConfigResult::Success();
    }
    if (!value.at("lens_correction").is_object()) {
        return ConfigResult::Failure("lens_correction",
                                     "missing or invalid value");
    }
    const ConfigJson &lens_correction = value.at("lens_correction");
    bool enabled = false;
    if (!json_utils::ReadField(lens_correction, "enabled", &enabled)) {
        return ConfigResult::Failure("lens_correction.enabled",
                                     "missing or invalid value");
    }
    if (enabled && !capabilities.lens_correction_supported) {
        return ConfigResult::Failure("lens_correction.enabled",
                                     "unsupported value");
    }
    if (enabled) {
        if (active_config.main_stream.size.width <
                capabilities.lens_correction_min_width ||
            active_config.main_stream.size.height <
                capabilities.lens_correction_min_height) {
            return ConfigResult::Failure("lens_correction.enabled",
                                         "main stream size unsupported");
        }
        if (active_config.sub_stream.enabled &&
            (active_config.sub_stream.size.width <
                 capabilities.lens_correction_min_width ||
             active_config.sub_stream.size.height <
                 capabilities.lens_correction_min_height)) {
            return ConfigResult::Failure("lens_correction.enabled",
                                         "sub stream size unsupported");
        }
    }
    const ConfigResult range_result = ValidateNumericControls(
        lens_correction, "lens_correction",
        capabilities.lens_correction_ranges);
    if (!range_result.ok) {
        return range_result;
    }
    const ConfigResult option_result = ValidateOptionControls(
        lens_correction, "lens_correction",
        capabilities.lens_correction_options);
    if (!option_result.ok) {
        return option_result;
    }
    return ConfigResult::Success();
}

ConfigResult ValidateStabilizationConfig(
    const ConfigJson &value,
    const ImageCapabilities &capabilities,
    const MediaPipelineConfig &active_config) {
    if (!value.contains("stabilization")) {
        return ConfigResult::Success();
    }
    if (!value.at("stabilization").is_object()) {
        return ConfigResult::Failure("stabilization",
                                     "missing or invalid value");
    }
    const ConfigJson &stabilization = value.at("stabilization");
    bool enabled = false;
    if (!json_utils::ReadField(stabilization, "enabled", &enabled)) {
        return ConfigResult::Failure("stabilization.enabled",
                                     "missing or invalid value");
    }
    if (enabled && !capabilities.stabilization_supported) {
        return ConfigResult::Failure("stabilization.enabled",
                                     "unsupported value");
    }
    if (enabled) {
        if (active_config.main_stream.size.width <
                capabilities.stabilization_min_width ||
            active_config.main_stream.size.height <
                capabilities.stabilization_min_height) {
            return ConfigResult::Failure("stabilization.enabled",
                                         "main stream size unsupported");
        }
        if (active_config.sub_stream.enabled &&
            (active_config.sub_stream.size.width <
                 capabilities.stabilization_min_width ||
             active_config.sub_stream.size.height <
                 capabilities.stabilization_min_height)) {
            return ConfigResult::Failure("stabilization.enabled",
                                         "sub stream size unsupported");
        }
    }
    const ConfigResult range_result = ValidateNumericControls(
        stabilization, "stabilization", capabilities.stabilization_ranges);
    if (!range_result.ok) {
        return range_result;
    }
    const ConfigResult option_result = ValidateOptionControls(
        stabilization, "stabilization", capabilities.stabilization_options);
    if (!option_result.ok) {
        return option_result;
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
        return ConfigResult::Failure(JoinField(stream_prefix, "codec"),
                                     "unsupported value");
    }
    if (!ContainsResolution(stream_capabilities, stream.resolution)) {
        return ConfigResult::Failure(JoinField(stream_prefix, "resolution"),
                                     "unsupported value");
    }
    if (stream.fps < stream_capabilities.frame_rate.min_fps ||
        stream.fps > stream_capabilities.frame_rate.max_fps) {
        return ConfigResult::Failure(JoinField(stream_prefix, "fps"),
                                     "unsupported value");
    }
    if (stream.bitrate_kbps < stream_capabilities.bitrate.min_kbps ||
        stream.bitrate_kbps > stream_capabilities.bitrate.max_kbps) {
        return ConfigResult::Failure(JoinField(stream_prefix, "bitrate_kbps"),
                                     "unsupported value");
    }
    if (!ContainsRateControl(stream_capabilities, stream.rate_control)) {
        return ConfigResult::Failure(JoinField(stream_prefix, "rate_control"),
                                     "unsupported value");
    }
    if (stream.gop < stream_capabilities.gop.min ||
        stream.gop > stream_capabilities.gop.max) {
        return ConfigResult::Failure(JoinField(stream_prefix, "gop"),
                                     "unsupported value");
    }
    if (stream.smart_codec && !stream_capabilities.smart_codec_supported) {
        return ConfigResult::Failure(JoinField(stream_prefix, "smart_codec"),
                                     "unsupported value");
    }
    if (stream.gop_mode == GopMode::kSmartP &&
        !stream_capabilities.smart_codec_supported) {
        return ConfigResult::Failure(JoinField(stream_prefix, "gop_mode"),
                                     "unsupported value");
    }
    if ((stream.smart_codec || stream.gop_mode == GopMode::kSmartP) &&
        stream.codec != VideoCodec::kH264 && stream.codec != VideoCodec::kH265) {
        return ConfigResult::Failure(JoinField(stream_prefix, "smart_codec"),
                                     "unsupported codec");
    }
    if (stream.roi.enabled || !stream.roi.regions.empty()) {
        if (!stream_capabilities.roi_supported ||
            stream_capabilities.max_roi_regions == 0) {
            return ConfigResult::Failure(JoinField(stream_prefix, "roi"),
                                         "unsupported value");
        }
        if (stream.codec != VideoCodec::kH264 &&
            stream.codec != VideoCodec::kH265) {
            return ConfigResult::Failure(JoinField(stream_prefix, "roi"),
                                         "unsupported codec");
        }
        const uint32_t max_roi_regions =
            stream_capabilities.max_roi_regions < kMaxVideoRoiRegions
                ? stream_capabilities.max_roi_regions
                : kMaxVideoRoiRegions;
        if (stream.roi.regions.size() > max_roi_regions) {
            return ConfigResult::Failure(JoinField(stream_prefix, "roi"),
                                         "too many regions");
        }
        for (size_t i = 0; i < stream.roi.regions.size(); ++i) {
            const VideoRoiRegion &region = stream.roi.regions[i];
            const std::string region_prefix =
                JoinField(JoinField(JoinField(stream_prefix, "roi"),
                                    "regions"),
                          std::to_string(i));
            if (region.width == 0 || region.height == 0) {
                return ConfigResult::Failure(region_prefix,
                                             "invalid region size");
            }
            if (region.x >= stream.resolution.width ||
                region.y >= stream.resolution.height ||
                region.width > stream.resolution.width - region.x ||
                region.height > stream.resolution.height - region.y) {
                return ConfigResult::Failure(region_prefix,
                                             "region outside stream frame");
            }
            if (region.qp < kMinVideoRoiQp || region.qp > kMaxVideoRoiQp) {
                return ConfigResult::Failure(JoinField(region_prefix, "qp"),
                                             "unsupported value");
            }
        }
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
    target->gop_mode = source.smart_codec ? GopMode::kSmartP
                                          : source.gop_mode;
    target->roi = source.roi;
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
    json["roi"] = stream.roi;
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
    const auto roi = json.find("roi");
    if (roi != json.end()) {
        roi->get_to(stream.roi);
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
        return ConfigResult::Failure("", "invalid video config");
    }
    if (!value.is_object()) {
        return ConfigResult::Failure("", "invalid video config");
    }

    if (!value.contains("streams") || !value.at("streams").is_object()) {
        return ConfigResult::Failure("", "invalid video config");
    }
    const ConfigJson &streams = value.at("streams");

    if (!streams.contains("main") || !streams.at("main").is_object()) {
        return ConfigResult::Failure("", "invalid video config");
    }
    const ConfigJson &main_stream = streams.at("main");
    if (!ValidateVideoStreamJson(main_stream)) {
        return ConfigResult::Failure("", "invalid video config");
    }

    if (!streams.contains("sub") || !streams.at("sub").is_object()) {
        return ConfigResult::Failure("", "invalid video config");
    }
    const ConfigJson &sub_stream = streams.at("sub");
    if (!ValidateVideoStreamJson(sub_stream)) {
        return ConfigResult::Failure("", "invalid video config");
    }

    value.get_to(*config);
    return ConfigResult::Success();
}

ConfigResult ValidateVideoConfig(const VideoConfig &config,
                                 const MediaCapabilities &capabilities) {
    if (capabilities.streams.empty()) {
        return ConfigResult::Failure("", "media capabilities unavailable");
    }
    if (!config.main.enabled) {
        return ConfigResult::Failure("streams.main.enabled",
                                     "main stream must stay enabled");
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
        return ConfigResult::Failure("", "invalid video config");
    }
    MediaPipelineConfig next_config = fallback;
    ApplyStreamConfig(StreamId::kMain, config.main, &next_config.main_stream);
    ApplyStreamConfig(StreamId::kSub, config.sub, &next_config.sub_stream);
    *pipeline_config = next_config;
    return ConfigResult::Success();
}

ConfigResult ValidateImageConfig(const ConfigJson &value,
                                 const ImageCapabilities &capabilities,
                                 const MediaPipelineConfig &active_config) {
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
        if (!value.contains(section_spec.name) ||
            !value.at(section_spec.name).is_object()) {
            return ConfigResult::Failure("", "invalid image config");
        }
        const ConfigJson &section = value.at(section_spec.name);
        if (section_spec.ranges != nullptr) {
            const ConfigResult result = ValidateNumericControls(
                section, section_spec.name, *section_spec.ranges);
            if (!result.ok) {
                return result;
            }
        }
        if (section_spec.options != nullptr) {
            const ConfigResult result = ValidateOptionControls(
                section, section_spec.name, *section_spec.options);
            if (!result.ok) {
                return result;
            }
        }
    }

    if (!value.contains("orientation") ||
        !value.at("orientation").is_object()) {
        return ConfigResult::Failure("", "invalid image config");
    }
    const ConfigJson &orientation = value.at("orientation");
    bool mirror = false;
    if (!json_utils::ReadField(orientation, "mirror", &mirror)) {
        return ConfigResult::Failure("orientation.mirror",
                                     "missing or invalid value");
    }
    if (mirror && !capabilities.mirror_supported) {
        return ConfigResult::Failure("orientation.mirror", "unsupported value");
    }
    bool flip = false;
    if (!json_utils::ReadField(orientation, "flip", &flip)) {
        return ConfigResult::Failure("orientation.flip",
                                     "missing or invalid value");
    }
    if (flip && !capabilities.flip_supported) {
        return ConfigResult::Failure("orientation.flip", "unsupported value");
    }

    if (value.contains("strategy") && value.at("strategy").is_object()) {
        const ConfigJson &strategy = value.at("strategy");
        const std::string mode = strategy.value("mode", "low_noise");
        if (mode != "balanced" && mode != "low_noise" && mode != "detail") {
            return ConfigResult::Failure("strategy.mode", "unsupported value");
        }
    }
    ConfigResult result =
        ValidateLensCorrectionConfig(value, capabilities, active_config);
    if (!result.ok) {
        return result;
    }
    return ValidateStabilizationConfig(value, capabilities, active_config);
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
