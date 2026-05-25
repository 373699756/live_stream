#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "config_service.h"
#include "json_utils.h"
#include "media/media_capabilities.h"
#include "media_service.h"
#include "infra/log.h"
#include "stream_browser_source.h"
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

ConfigJson MediaCapabilitiesToJson(const MediaCapabilities &capabilities) {
    ConfigJson root = ConfigJson::object();
    ConfigJson streams = ConfigJson::object();
    for (const VideoStreamCapabilities &stream : capabilities.streams) {
        const char *name = StreamIdToJsonString(stream.stream_id);
        if (std::strcmp(name, "unknown") != 0) {
            streams[name] = StreamCapabilitiesToJson(stream, true);
        }
    }
    root["streams"] = streams;
    root["image"] = ImageCapabilitiesToJson(capabilities.image);
    return root;
}

ConfigJson ImageStrategyStatusToJson(const ImageStrategyStatus &status) {
    ConfigJson root = ConfigJson::object();
    root["enabled"] = status.enabled;
    root["active"] = status.active;
    root["exposure_valid"] = status.exposure_valid;
    root["iso"] = status.iso;
    root["exposure_time_us"] = status.exposure_time_us;
    root["analog_gain"] = status.analog_gain;
    root["digital_gain"] = status.digital_gain;
    root["isp_digital_gain"] = status.isp_digital_gain;
    root["mode"] = status.mode;
    root["tier"] = status.tier;
    root["saturation"] = status.saturation;
    root["sharpness"] = status.sharpness;
    root["denoise_2d"] = status.denoise_2d;
    root["denoise_3d"] = status.denoise_3d;
    root["gamma"] = status.gamma;
    return root;
}

bool HasReadyBrowserProtocol(const StreamBrowserStatus &status) {
    return status.hls_ready || status.flv_ready || status.mjpeg_ready;
}

void RequestBrowserRecoveryKeyFrame(IStreamBrowserSource *stream_browser_source,
                                    StreamId stream_id,
                                    const StreamBrowserStatus &status) {
    if (stream_browser_source == nullptr || !status.running ||
        !status.browser_codec || HasReadyBrowserProtocol(status)) {
        return;
    }
    (void)stream_browser_source->RequestKeyFrame(stream_id,
                                              KeyFrameReason::kRecovery);
}

}  // namespace

class MediaHttpHandler : public IHttpHandler {
public:
    MediaHttpHandler(HttpAccess *access,
                     IConfigService *config_service,
                     IMediaService *media_service,
                     IStreamBrowserSource *stream_browser_source,
                     IWebrtcService *webrtc_service)
        : access_(access),
          config_service_(config_service),
          media_service_(media_service),
          stream_browser_source_(stream_browser_source),
          webrtc_service_(webrtc_service) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/media/capabilities",
                              &MediaHttpHandler::HandleCapabilitiesRoute,
                              this);
        router->AddExactRoute(HttpMethod::kGet, "/api/status/streams",
                              &MediaHttpHandler::HandleStreamStatusRoute,
                              this);
        router->AddExactRoute(HttpMethod::kGet, "/api/status/image-strategy",
                              &MediaHttpHandler::HandleImageStrategyRoute,
                              this);
    }

private:
    static HttpResponse HandleCapabilitiesRoute(void *user,
                                                const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleCapabilities(
            request);
    }

    static HttpResponse HandleStreamStatusRoute(void *user,
                                                const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleStreamStatus(
            request);
    }

    static HttpResponse HandleImageStrategyRoute(void *user,
                                                 const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleImageStrategy(
            request);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        if (media_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReadStatus,
                                         "media", &principal)) {
            return ForbiddenResponse(principal);
        }
        MediaCapabilities capabilities =
            media_service_->GetCapabilities();
        if (capabilities.streams.empty()) {
            return StatusResponse(500, "Media capabilities unavailable");
        }
        return JsonResponse(200, MediaCapabilitiesToJson(capabilities));
    }

    HttpResponse HandleStreamStatus(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReadStatus,
                                         "media", &principal)) {
            return ForbiddenResponse(principal);
        }
        if (media_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }

        ConfigJson items = ConfigJson::array();
        ConfigJson video_config =
            config_service_->GetValue("video");
        if (!video_config.is_object() || !video_config.contains("streams") ||
            !video_config.at("streams").is_object()) {
            return StatusResponse(500, "Invalid video config");
        }
        const ConfigJson &streams = video_config.at("streams");
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
            if (!json_utils::ReadField(stream, "codec", &codec) ||
                !json_utils::ReadField(stream, "resolution", &resolution) ||
                !json_utils::ReadField(stream, "fps", &fps, 1,
                                  std::numeric_limits<int64_t>::max()) ||
                !json_utils::ReadField(stream, "bitrate_kbps", &bitrate_kbps, 1,
                                  std::numeric_limits<int64_t>::max()) ||
                !json_utils::ReadField(stream, "enabled", &stream_enabled)) {
                return StatusResponse(500, "Invalid video config");
            }
            item["codec"] = codec;
            item["resolution"] = resolution;
            item["fps"] = fps;
            item["bitrateKbps"] = bitrate_kbps;
            StreamId stream_id = StreamId::kMain;
            (void)StreamIdFromJsonString(name, &stream_id);
            const bool stream_running =
                media_service_->IsStreamStarted(stream_id);
            item["state"] = media_service_->IsRestarting()
                                ? "pending"
                                : (stream_running && stream_enabled
                                       ? "running"
                                       : "stopped");
            if (stream_browser_source_ != nullptr) {
                const StreamBrowserStatus browser =
                    stream_browser_source_->GetBrowserStatus(
                        stream_id);
                RequestBrowserRecoveryKeyFrame(stream_browser_source_,
                                               stream_id, browser);
                item["browserCodec"] = browser.browser_codec;
                item["hlsSupported"] = browser.hls_supported;
                item["flvSupported"] = browser.flv_supported;
                item["mjpegSupported"] = browser.mjpeg_supported;
                item["hlsReady"] = browser.hls_ready;
                item["flvReady"] = browser.flv_ready;
                item["mjpegReady"] = browser.mjpeg_ready;
                if (browser.running && browser.browser_codec &&
                    !HasReadyBrowserProtocol(browser)) {
                    INFRA_LOG_WARN(
                        kHttpModuleName,
                        "stream browser not ready stream=%s codec=%s "
                        "hls_ready=%d flv_ready=%d mjpeg_ready=%d segments=%u "
                        "current_segment=%u flv_header=%u flv_keyframe=%u",
                        name, VideoCodecToJsonString(browser.codec),
                        browser.hls_ready ? 1 : 0,
                        browser.flv_ready ? 1 : 0,
                        browser.mjpeg_ready ? 1 : 0,
                        browser.hls_segment_count,
                        browser.hls_current_segment_size,
                        browser.flv_sequence_header_size,
                        browser.flv_last_keyframe_size);
                }
            } else {
                item["browserCodec"] = false;
                item["hlsSupported"] = false;
                item["flvSupported"] = false;
                item["mjpegSupported"] = false;
                item["hlsReady"] = false;
                item["flvReady"] = false;
                item["mjpegReady"] = false;
            }
            WebrtcServiceStats webrtc_stats;
            if (webrtc_service_ != nullptr) {
                webrtc_stats = webrtc_service_->GetStats();
            }
            item["webrtcReady"] = stream_running && stream_enabled &&
                                  (codec == "h264" || codec == "h265") &&
                                  webrtc_stats.enabled &&
                                  webrtc_stats.backend_available;
            items.push_back(item);
        }
        return JsonResponse(200, items);
    }

    HttpResponse HandleImageStrategy(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReadStatus,
                                         "image-strategy", &principal)) {
            return ForbiddenResponse(principal);
        }
        if (media_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        return JsonResponse(
            200,
            ImageStrategyStatusToJson(
                media_service_->GetImageStrategyStatus()));
    }

    HttpAccess *access_ = nullptr;
    IConfigService *config_service_ = nullptr;
    IMediaService *media_service_ = nullptr;
    IStreamBrowserSource *stream_browser_source_ = nullptr;
    IWebrtcService *webrtc_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateMediaHttpHandler(HttpAccess *access,
                       IConfigService *config_service,
                       IMediaService *media_service,
                       IStreamBrowserSource *stream_browser_source,
                       IWebrtcService *webrtc_service) {
    return std::unique_ptr<IHttpHandler>(
        new MediaHttpHandler(access, config_service, media_service,
                             stream_browser_source, webrtc_service));
}

}  // namespace live_stream
