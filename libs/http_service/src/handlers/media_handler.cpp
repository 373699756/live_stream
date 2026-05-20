#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "config_service.h"
#include "live_stream/json_utils.h"
#include "media/media_capabilities.h"
#include "media_service.h"
#include "infra/log.h"
#include "stream_hub_service.h"
#include "webrtc_service.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace live_stream {
namespace {

const char *VideoCodecToJsonString(VideoCodec codec) {
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
    return "unknown";
}

const char *RateControlModeToJsonString(RateControlMode mode) {
    switch (mode) {
        case RateControlMode::kCbr:
            return "cbr";
        case RateControlMode::kVbr:
            return "vbr";
        case RateControlMode::kFixQp:
            return "fixqp";
    }
    return "unknown";
}

ConfigJson VideoResolutionToJson(const VideoResolution &resolution) {
    ConfigJson root = ConfigJson::object();
    root["width"] = resolution.width;
    root["height"] = resolution.height;
    return root;
}

ConfigJson CodecCapabilityToJson(const CodecCapability &capability) {
    ConfigJson root = ConfigJson::object();
    root["codec"] = VideoCodecToJsonString(capability.codec);
    ConfigJson profiles = ConfigJson::array();
    for (const std::string &profile : capability.profiles) {
        profiles.push_back(profile);
    }
    root["profiles"] = profiles;
    return root;
}

ConfigJson StreamCapabilitiesToJson(const VideoStreamCapabilities &stream,
                                    bool available) {
    ConfigJson root = ConfigJson::object();
    root["stream"] = StreamIdToJsonString(stream.stream_id);
    root["available"] = available;

    ConfigJson codecs = ConfigJson::array();
    for (const CodecCapability &capability : stream.codecs) {
        codecs.push_back(CodecCapabilityToJson(capability));
    }
    root["codecs"] = codecs;

    ConfigJson resolutions = ConfigJson::array();
    for (const VideoResolution &resolution : stream.resolutions) {
        resolutions.push_back(VideoResolutionToJson(resolution));
    }
    root["resolutions"] = resolutions;

    ConfigJson fps = ConfigJson::object();
    fps["min"] = stream.frame_rate.min_fps;
    fps["max"] = stream.frame_rate.max_fps;
    root["fps"] = fps;

    ConfigJson bitrate = ConfigJson::object();
    bitrate["min"] = stream.bitrate.min_kbps;
    bitrate["max"] = stream.bitrate.max_kbps;
    root["bitrate_kbps"] = bitrate;

    ConfigJson rate_control = ConfigJson::array();
    for (RateControlMode mode : stream.rate_control_modes) {
        rate_control.push_back(RateControlModeToJsonString(mode));
    }
    root["rate_control"] = rate_control;

    ConfigJson gop = ConfigJson::object();
    gop["min"] = stream.gop.min;
    gop["max"] = stream.gop.max;
    root["gop"] = gop;
    root["smart_codec"] = stream.smart_codec_supported;
    return root;
}

ConfigJson
NumericControlsToJson(const std::vector<NumericControlCapability> &controls) {
    ConfigJson root = ConfigJson::object();
    for (const NumericControlCapability &control : controls) {
        ConfigJson value = ConfigJson::object();
        value["min"] = control.min;
        value["max"] = control.max;
        value["default"] = control.default_value;
        value["runtime_supported"] = control.runtime_supported;
        root[control.name] = value;
    }
    return root;
}

ConfigJson
OptionControlsToJson(const std::vector<OptionControlCapability> &controls) {
    ConfigJson root = ConfigJson::object();
    for (const OptionControlCapability &control : controls) {
        ConfigJson values = ConfigJson::array();
        for (const std::string &value : control.values) {
            values.push_back(value);
        }
        ConfigJson item = ConfigJson::object();
        item["values"] = values;
        item["default"] = control.default_value;
        item["runtime_supported"] = control.runtime_supported;
        root[control.name] = item;
    }
    return root;
}

ConfigJson ImageCapabilitiesToJson(const ImageCapabilities &image) {
    ConfigJson root = ConfigJson::object();
    root["basic"] = NumericControlsToJson(image.basic);

    ConfigJson exposure = ConfigJson::object();
    exposure["options"] = OptionControlsToJson(image.exposure_options);
    exposure["ranges"] = NumericControlsToJson(image.exposure_ranges);
    root["exposure"] = exposure;

    ConfigJson white_balance = ConfigJson::object();
    white_balance["options"] = OptionControlsToJson(image.white_balance_options);
    white_balance["ranges"] = NumericControlsToJson(image.white_balance_ranges);
    root["white_balance"] = white_balance;

    ConfigJson enhancement = ConfigJson::object();
    enhancement["options"] = OptionControlsToJson(image.enhancement_options);
    enhancement["ranges"] = NumericControlsToJson(image.enhancement_ranges);
    root["enhancement"] = enhancement;

    ConfigJson backlight = ConfigJson::object();
    backlight["options"] = OptionControlsToJson(image.backlight_options);
    backlight["ranges"] = NumericControlsToJson(image.backlight_ranges);
    root["backlight"] = backlight;

    root["color_mode"] = OptionControlsToJson(image.color_mode_options);
    root["orientation"]["mirror"] = image.mirror_supported;
    root["orientation"]["flip"] = image.flip_supported;
    return root;
}

ConfigJson MediaCapabilitiesToJson(const MediaCapabilities &capabilities,
                                   const IMediaView *media_service) {
    ConfigJson root = ConfigJson::object();
    ConfigJson streams = ConfigJson::object();
    for (const VideoStreamCapabilities &stream : capabilities.streams) {
        const char *name = StreamIdToJsonString(stream.stream_id);
        if (std::strcmp(name, "unknown") != 0) {
            const bool available = media_service == nullptr ||
                                   media_service->IsStreamStarted(stream.stream_id);
            streams[name] = StreamCapabilitiesToJson(stream, available);
        }
    }
    root["streams"] = streams;
    root["image"] = ImageCapabilitiesToJson(capabilities.image);
    return root;
}

void RequestBrowserRecoveryKeyFrame(HttpHandlerContext *context,
                                    StreamId stream_id,
                                    const StreamBrowserStatus &status) {
    if (context == nullptr ||
        context->Dependencies().stream_hub_service == nullptr ||
        !status.running || !status.browser_codec ||
        (status.hls_ready && status.flv_ready)) {
        return;
    }
    (void)context->Dependencies().stream_hub_service->RequestKeyFrame(
        stream_id, KeyFrameReason::kRecovery);
}

}  // namespace

HttpResponse http_handlers::HandleMediaCapabilities(HttpHandlerContext *context) {
    if (context->Dependencies().media_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    MediaCapabilities capabilities =
        context->Dependencies().media_service->GetCapabilities();
    if (capabilities.streams.empty()) {
        return StatusResponse(500, "Media capabilities unavailable");
    }
    return JsonResponse(200, MediaCapabilitiesToJson(
                                 capabilities, context->Dependencies().media_service));
}

HttpResponse http_handlers::HandleStreamStatus(HttpHandlerContext *context, const HttpRequest &request) {
    AuthPrincipal principal = context->Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    if (context->Dependencies().media_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }

    ConfigJson items = ConfigJson::array();
    ConfigJson video_config = context->Dependencies().config_service->GetValue("video");
    if (!video_config.is_object() || !video_config.contains("streams") ||
        !video_config["streams"].is_object()) {
        return StatusResponse(500, "Invalid video config");
    }
    const ConfigJson &streams = video_config["streams"];
    const char *names[] = {"main", "sub"};
    for (const char *name : names) {
        ConfigJson item = ConfigJson::object();
        item["stream"] = name;
        if (!streams.contains(name) || !streams.at(name).is_object()) {
            return StatusResponse(500, "Invalid video config");
        }
        const ConfigJson &stream = streams.at(name);
        std::string codec;
        std::string resolution;
        int64_t fps = 0;
        int64_t bitrate_kbps = 0;
        bool stream_enabled = false;
        if (!json_utils::Load(stream, "codec", &codec) ||
            !json_utils::Load(stream, "resolution", &resolution) ||
            !json_utils::Load(stream, "fps", &fps, 1,
                              std::numeric_limits<int64_t>::max()) ||
            !json_utils::Load(stream, "bitrate_kbps", &bitrate_kbps, 1,
                              std::numeric_limits<int64_t>::max()) ||
            !json_utils::Load(stream, "enabled", &stream_enabled)) {
            return StatusResponse(500, "Invalid video config");
        }
        item["codec"] = codec;
        item["resolution"] = resolution;
        item["fps"] = fps;
        item["bitrateKbps"] = bitrate_kbps;
        StreamId stream_id = StreamId::kMain;
        (void)StreamIdFromJsonString(name, &stream_id);
        const bool stream_running =
            context->Dependencies().media_service->IsStreamStarted(stream_id);
        item["state"] =
            context->Dependencies().media_service->IsRestarting()
                ? "pending"
                : (stream_running && stream_enabled ? "running" : "stopped");
        if (context->Dependencies().stream_hub_service != nullptr) {
            const StreamBrowserStatus browser =
                context->Dependencies().stream_hub_service->GetBrowserStatus(stream_id);
            RequestBrowserRecoveryKeyFrame(context, stream_id, browser);
            item["browserCodec"] = browser.browser_codec;
            item["hlsReady"] = browser.hls_ready;
            item["flvReady"] = browser.flv_ready;
            if (browser.running && browser.browser_codec &&
                (!browser.hls_ready || !browser.flv_ready)) {
                INFRA_LOG_WARN(
                    kHttpModuleName,
                    "stream browser not ready stream=%s codec=%s hls_ready=%d "
                    "flv_ready=%d segments=%u current_segment=%u "
                    "flv_header=%u flv_keyframe=%u",
                    name, VideoCodecToJsonString(browser.codec),
                    browser.hls_ready ? 1 : 0, browser.flv_ready ? 1 : 0,
                    browser.hls_segment_count,
                    browser.hls_current_segment_size,
                    browser.flv_sequence_header_size,
                    browser.flv_last_keyframe_size);
            }
        } else {
            item["browserCodec"] = false;
            item["hlsReady"] = false;
            item["flvReady"] = false;
        }
        WebrtcServiceStats webrtc_stats;
        if (context->Dependencies().webrtc_service != nullptr) {
            webrtc_stats = context->Dependencies().webrtc_service->GetStats();
        }
        item["webrtcReady"] = stream_running && stream_enabled &&
                              codec == "h264" && webrtc_stats.enabled &&
                              webrtc_stats.backend_available;
        items.push_back(item);
    }
    return JsonResponse(200, items);
}

}  // namespace live_stream
