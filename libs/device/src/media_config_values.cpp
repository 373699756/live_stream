#include "media_config_values.h"

#include "json.h"
#include "media_config_codec.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace media_config_values {

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

}  // namespace media_config_values

void to_json(Json &json, const VideoSize &size) {
    json = std::to_string(size.width) + "x" + std::to_string(size.height);
}

void from_json(const Json &json, VideoSize &size) {
    std::string text;
    json.get_to(text);

    VideoSize parsed;
    if (media_config_values::ParseResolutionText(text, &parsed)) {
        size = parsed;
    }
}

void to_json(Json &json, const Codec &codec) {
    json = media_config_values::CodecToString(codec);
}

void from_json(const Json &json, Codec &codec) {
    std::string text;
    json.get_to(text);
    (void)media_config_values::ParseCodecText(text, &codec);
}

void to_json(Json &json, const RateControlMode &mode) {
    json = media_config_values::RateControlToString(mode);
}

void from_json(const Json &json, RateControlMode &mode) {
    std::string text;
    json.get_to(text);
    (void)media_config_values::ParseRateControlText(text, &mode);
}

void to_json(Json &json, const GopMode &mode) {
    json = media_config_values::GopModeToString(mode);
}

void from_json(const Json &json, GopMode &mode) {
    std::string text;
    json.get_to(text);
    (void)media_config_values::ParseGopModeText(text, &mode);
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

}  // namespace live_stream
