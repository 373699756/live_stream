#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "config.h"
#include "http_protocol.h"
#include "json_utils.h"
#include "device.h"
#include "infra/log.h"
#include "rtsp.h"
#include "webrtc.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace {

const char *CodecToJsonString(Codec codec) {
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
    return "unknown";
}

const char *RateControlToJsonString(RateControlMode mode) {
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

ConfigJson NumericControlToJson(const NumericControlCapability &capability) {
    ConfigJson root = ConfigJson::object();
    root["min"] = capability.min;
    root["max"] = capability.max;
    root["default"] = capability.default_value;
    root["live_update_supported"] = capability.live_update_supported;
    return root;
}

ConfigJson OptionControlToJson(const OptionControlCapability &capability) {
    ConfigJson root = ConfigJson::object();
    ConfigJson values = ConfigJson::array();
    for (const std::string &value : capability.values) {
        values.push_back(value);
    }
    root["values"] = values;
    root["default"] = capability.default_value;
    root["live_update_supported"] = capability.live_update_supported;
    return root;
}

ConfigJson NumericControlsToJson(
    const std::vector<NumericControlCapability> &capabilities) {
    ConfigJson root = ConfigJson::object();
    for (const NumericControlCapability &capability : capabilities) {
        root[capability.name] = NumericControlToJson(capability);
    }
    return root;
}

ConfigJson OptionControlsToJson(
    const std::vector<OptionControlCapability> &capabilities) {
    ConfigJson root = ConfigJson::object();
    for (const OptionControlCapability &capability : capabilities) {
        root[capability.name] = OptionControlToJson(capability);
    }
    return root;
}

ConfigJson VideoStreamCapabilitiesToJson(
    StreamId stream_id, const VideoStreamCapabilities *capabilities) {
    ConfigJson root = ConfigJson::object();
    root["stream"] = StreamIdToJsonString(stream_id);
    root["available"] = capabilities != nullptr;
    root["codecs"] = ConfigJson::array();
    root["resolutions"] = ConfigJson::array();
    root["rate_control"] = ConfigJson::array();
    root["smart_codec"] = false;
    root["roi_supported"] = false;
    root["max_roi_regions"] = 0;
    if (capabilities == nullptr) {
        ConfigJson fps = ConfigJson::object();
        fps["min"] = 0;
        fps["max"] = 0;
        root["fps"] = fps;
        ConfigJson bitrate = ConfigJson::object();
        bitrate["min"] = 0;
        bitrate["max"] = 0;
        root["bitrate_kbps"] = bitrate;
        ConfigJson gop = ConfigJson::object();
        gop["min"] = 0;
        gop["max"] = 0;
        root["gop"] = gop;
        return root;
    }

    ConfigJson codecs = ConfigJson::array();
    for (const CodecCapability &codec : capabilities->codecs) {
        ConfigJson item = ConfigJson::object();
        item["codec"] = CodecToJsonString(codec.codec);
        ConfigJson profiles = ConfigJson::array();
        for (const std::string &profile : codec.profiles) {
            profiles.push_back(profile);
        }
        item["profiles"] = profiles;
        codecs.push_back(item);
    }
    root["codecs"] = codecs;

    ConfigJson resolutions = ConfigJson::array();
    for (const VideoResolution &resolution : capabilities->resolutions) {
        ConfigJson item = ConfigJson::object();
        item["width"] = resolution.width;
        item["height"] = resolution.height;
        resolutions.push_back(item);
    }
    root["resolutions"] = resolutions;

    ConfigJson fps = ConfigJson::object();
    fps["min"] = capabilities->frame_rate.min_fps;
    fps["max"] = capabilities->frame_rate.max_fps;
    root["fps"] = fps;

    ConfigJson bitrate = ConfigJson::object();
    bitrate["min"] = capabilities->bitrate.min_kbps;
    bitrate["max"] = capabilities->bitrate.max_kbps;
    root["bitrate_kbps"] = bitrate;

    ConfigJson rate_control = ConfigJson::array();
    for (RateControlMode mode : capabilities->rate_control_modes) {
        rate_control.push_back(RateControlToJsonString(mode));
    }
    root["rate_control"] = rate_control;

    ConfigJson gop = ConfigJson::object();
    gop["min"] = capabilities->gop.min;
    gop["max"] = capabilities->gop.max;
    root["gop"] = gop;
    root["smart_codec"] = capabilities->smart_codec_supported;
    root["roi_supported"] = capabilities->roi_supported;
    root["max_roi_regions"] = capabilities->max_roi_regions;
    return root;
}

const VideoStreamCapabilities *FindStreamCapabilities(
    const MediaCapabilities &capabilities, StreamId stream_id) {
    for (const VideoStreamCapabilities &stream : capabilities.streams) {
        if (stream.stream_id == stream_id) {
            return &stream;
        }
    }
    return nullptr;
}

ConfigJson ImageCapabilitiesToJson(const ImageCapabilities &capabilities) {
    ConfigJson root = ConfigJson::object();
    root["basic"] = NumericControlsToJson(capabilities.basic);

    ConfigJson exposure = ConfigJson::object();
    exposure["options"] =
        OptionControlsToJson(capabilities.exposure_options);
    exposure["ranges"] = NumericControlsToJson(capabilities.exposure_ranges);
    root["exposure"] = exposure;

    ConfigJson white_balance = ConfigJson::object();
    white_balance["options"] =
        OptionControlsToJson(capabilities.white_balance_options);
    white_balance["ranges"] =
        NumericControlsToJson(capabilities.white_balance_ranges);
    root["white_balance"] = white_balance;

    ConfigJson enhancement = ConfigJson::object();
    enhancement["options"] =
        OptionControlsToJson(capabilities.enhancement_options);
    enhancement["ranges"] =
        NumericControlsToJson(capabilities.enhancement_ranges);
    root["enhancement"] = enhancement;

    ConfigJson backlight = ConfigJson::object();
    backlight["options"] =
        OptionControlsToJson(capabilities.backlight_options);
    backlight["ranges"] = NumericControlsToJson(capabilities.backlight_ranges);
    root["backlight"] = backlight;

    root["color_mode"] =
        OptionControlsToJson(capabilities.color_mode_options);

    ConfigJson lens_correction = ConfigJson::object();
    lens_correction["supported"] = capabilities.lens_correction_supported;
    lens_correction["min_width"] = capabilities.lens_correction_min_width;
    lens_correction["min_height"] = capabilities.lens_correction_min_height;
    lens_correction["options"] =
        OptionControlsToJson(capabilities.lens_correction_options);
    lens_correction["ranges"] =
        NumericControlsToJson(capabilities.lens_correction_ranges);
    root["lens_correction"] = lens_correction;

    ConfigJson stabilization = ConfigJson::object();
    stabilization["supported"] = capabilities.stabilization_supported;
    stabilization["min_width"] = capabilities.stabilization_min_width;
    stabilization["min_height"] = capabilities.stabilization_min_height;
    stabilization["options"] =
        OptionControlsToJson(capabilities.stabilization_options);
    stabilization["ranges"] =
        NumericControlsToJson(capabilities.stabilization_ranges);
    root["stabilization"] = stabilization;

    ConfigJson orientation = ConfigJson::object();
    orientation["mirror"] = capabilities.mirror_supported;
    orientation["flip"] = capabilities.flip_supported;
    root["orientation"] = orientation;
    return root;
}

ConfigJson MediaCapabilitiesToJson(const MediaCapabilities &capabilities) {
    ConfigJson root = ConfigJson::object();
    ConfigJson streams = ConfigJson::object();
    streams["main"] = VideoStreamCapabilitiesToJson(
        StreamId::kMain,
        FindStreamCapabilities(capabilities, StreamId::kMain));
    streams["sub"] = VideoStreamCapabilitiesToJson(
        StreamId::kSub,
        FindStreamCapabilities(capabilities, StreamId::kSub));
    root["streams"] = streams;
    root["image"] = ImageCapabilitiesToJson(capabilities.image);
    return root;
}

bool IsPreviewProtocolReady(const MediaStreamInfo &stream_info) {
    return stream_info.hls_ready || stream_info.flv_ready ||
           stream_info.mjpeg_ready;
}

void RequestPreviewKeyframe(MediaStreams *media_streams,
                            StreamId stream_id,
                            const MediaStreamInfo &stream_info) {
    if (media_streams == nullptr || !stream_info.running ||
        !stream_info.preview_codec ||
        IsPreviewProtocolReady(stream_info)) {
        return;
    }
    (void)media_streams->RequestKeyframe(stream_id,
                                         KeyframeRequestSource::kRecovery);
}

bool IsWebrtcReady(IWebrtc *webrtc) {
    if (webrtc == nullptr) {
        return false;
    }
    const WebrtcStats stats = webrtc->GetStats();
    return stats.enabled && stats.signaling_ready && stats.ice_ready &&
           stats.dtls_ready && stats.srtp_ready;
}

bool IsWebrtcSupported(Codec codec, IWebrtc *webrtc) {
    if (webrtc == nullptr) {
        return false;
    }
    const WebrtcStats stats = webrtc->GetStats();
    return stats.enabled && (codec == Codec::kH264 ||
                             codec == Codec::kH265);
}

struct VideoStreamDisplayConfig {
    std::string resolution;
    uint32_t fps = 0;
    uint32_t bitrate_kbps = 0;
    bool has_resolution = false;
    bool has_fps = false;
    bool has_bitrate_kbps = false;
};

bool ReadVideoStreamDisplayConfig(IConfig *config,
                                  StreamId stream_id,
                                  VideoStreamDisplayConfig *display_config) {
    if (config == nullptr || display_config == nullptr) {
        return false;
    }
    const ConfigJson video = config->Get("video");
    if (!video.is_object()) {
        return false;
    }
    const auto streams = video.find("streams");
    if (streams == video.end() || !streams->is_object()) {
        return false;
    }
    const auto stream = streams->find(StreamIdToJsonString(stream_id));
    if (stream == streams->end() || !stream->is_object()) {
        return false;
    }

    std::string resolution;
    if (json_utils::ReadField(*stream, "resolution", &resolution) &&
        !resolution.empty()) {
        display_config->resolution = resolution;
        display_config->has_resolution = true;
    }
    uint32_t fps = 0;
    if (json_utils::ReadField(*stream, "fps", &fps) && fps > 0) {
        display_config->fps = fps;
        display_config->has_fps = true;
    }
    uint32_t bitrate_kbps = 0;
    if (json_utils::ReadField(*stream, "bitrate_kbps", &bitrate_kbps) &&
        bitrate_kbps > 0) {
        display_config->bitrate_kbps = bitrate_kbps;
        display_config->has_bitrate_kbps = true;
    }
    return display_config->has_resolution || display_config->has_fps ||
           display_config->has_bitrate_kbps;
}

void AddVideoStreamDisplayConfig(ConfigJson *root,
                                 const VideoStreamDisplayConfig &config) {
    if (root == nullptr) {
        return;
    }
    if (config.has_resolution) {
        (*root)["resolution"] = config.resolution;
    }
    if (config.has_fps) {
        (*root)["fps"] = config.fps;
    }
    if (config.has_bitrate_kbps) {
        (*root)["bitrate_kbps"] = config.bitrate_kbps;
    }
}

ConfigJson MediaStreamInfoToJson(StreamId stream_id,
                                 IConfig *config,
                                 DeviceMedia *device,
                                 MediaStreams *media_streams,
                                 IWebrtc *webrtc) {
    MediaStreamInfo stream_info;
    MediaStreamStats stats;
    bool media_stream_available = false;
    if (media_streams != nullptr) {
        stream_info = media_streams->GetStreamInfo(stream_id);
        stats = media_streams->GetStreamStats();
        media_stream_available = media_streams->IsStreamAvailable(stream_id);
        RequestPreviewKeyframe(media_streams, stream_id, stream_info);
    }

    const bool device_stream_running =
        device != nullptr && device->IsStreamStarted(stream_id);
    const bool stream_running = stream_info.running || device_stream_running;
    const Codec codec =
        media_streams != nullptr ? stream_info.codec
                                 : (device != nullptr
                                        ? device->GetStreamCodec(stream_id)
                                        : Codec::kH264);

    ConfigJson root = ConfigJson::object();
    root["stream"] = StreamIdToJsonString(stream_id);
    root["available"] = media_stream_available || device != nullptr;
    root["running"] = stream_running;
    root["codec"] = CodecToJsonString(codec);
    root["codec_generation"] = stream_info.codec_generation;
    root["track_ready"] = stream_info.track_ready;
    root["hls_supported"] = stream_info.hls_supported;
    root["hls_ready"] = stream_info.hls_ready;
    root["http_flv_supported"] = stream_info.flv_supported;
    root["http_flv_ready"] = stream_info.flv_ready;
    root["mjpeg_supported"] = stream_info.mjpeg_supported;
    root["mjpeg_ready"] = stream_info.mjpeg_ready;
    const bool webrtc_supported = IsWebrtcSupported(codec, webrtc);
    root["webrtc_supported"] = webrtc_supported;
    root["webrtc_ready"] =
        stream_running && stream_info.track_ready && webrtc_supported &&
        IsWebrtcReady(webrtc);
    root["subscription_count"] = stats.active_subscriptions;
    root["client_count"] =
        stats.active_flv_clients + stats.active_mjpeg_clients;
    root["cached_frames"] = stats.cached_frames;
    root["cached_bytes"] = stats.cached_bytes;
    root["hls_bytes"] = stream_info.hls_current_segment_size;
    root["last_dts"] = stream_info.last_dts_us;
    root["last_keyframe_request_ms"] = 0;
    root["last_keyframe_seen_ms"] = 0;
    root["last_first_frame_ms"] = 0;
    root["last_protocol_ready_ms"] = 0;
    root["last_reset_reason"] = stream_info.last_reset_reason;
    VideoStreamDisplayConfig display_config;
    if (ReadVideoStreamDisplayConfig(config, stream_id, &display_config)) {
        AddVideoStreamDisplayConfig(&root, display_config);
    }
    return root;
}

std::string HostWithoutPort(const std::string &host_header) {
    if (host_header.empty()) {
        return std::string();
    }
    if (host_header[0] == '[') {
        const size_t close = host_header.find(']');
        return close == std::string::npos
                   ? host_header
                   : host_header.substr(1, close - 1);
    }
    const size_t colon = host_header.find(':');
    return colon == std::string::npos ? host_header
                                      : host_header.substr(0, colon);
}

std::string AdvertiseHostFromConfig(IConfig *config) {
    if (config == nullptr) {
        return std::string();
    }
    ConfigJson network = config->Get("network");
    std::string advertise_host;
    if (network.is_object() &&
        json_utils::ReadField(network, "advertise_ip", &advertise_host)) {
        return advertise_host;
    }
    return std::string();
}

uint16_t RtspPortFromConfig(IConfig *config, uint16_t fallback) {
    if (config == nullptr) {
        return fallback;
    }
    ConfigJson rtsp = config->Get("rtsp");
    int64_t port = 0;
    if (rtsp.is_object() &&
        json_utils::ReadField(rtsp, "port", &port, 1, 65535)) {
        return static_cast<uint16_t>(port);
    }
    ConfigJson network = config->Get("network");
    if (network.is_object() && network.contains("ports") &&
        network.at("ports").is_object() &&
        json_utils::ReadField(network.at("ports"), "rtsp", &port, 1, 65535)) {
        return static_cast<uint16_t>(port);
    }
    return fallback;
}

std::string BuildRtspUrl(IConfig *config, IRtsp *rtsp,
                         const HttpRequest &request, StreamId stream_id) {
    if (rtsp == nullptr) {
        return std::string();
    }
    RtspListenAddress address = rtsp->LocalAddress();
    address.port = RtspPortFromConfig(config, address.port);
    if (address.port == 0) {
        return std::string();
    }
    std::string host = HostWithoutPort(GetHeader(request, "Host"));
    if (host.empty()) {
        host = AdvertiseHostFromConfig(config);
    }
    if (host.empty() || host == "0.0.0.0") {
        host = address.ip;
    }
    return BuildRtspStreamUrl(address, stream_id, host);
}

ConfigJson PreviewUrlsToJson(IConfig *config, IRtsp *rtsp,
                             const HttpRequest &request,
                             StreamId stream_id) {
    const std::string stream = StreamIdToJsonString(stream_id);
    ConfigJson root = ConfigJson::object();
    root["stream"] = stream;
    root["hls"] = "/live/" + stream + "/hls/index.m3u8";
    root["http_flv"] = "/live/" + stream + ".live.flv";
    root["mjpeg"] = "/live/" + stream + ".mjpg";
    root["snapshot"] = "/snapshot/" + stream + ".jpg";
    root["rtsp"] = BuildRtspUrl(config, rtsp, request, stream_id);
    root["webrtc_whep"] = "/live/" + stream + "/whep";
    return root;
}

const char *RtspTransportToJsonString(RtspTransportMode transport) {
    return transport == RtspTransportMode::kUdp ? "udp"
                                                : "tcp_interleaved";
}

ConfigJson RtspSessionToJson(const RtspSessionInfo &session) {
    ConfigJson root = ConfigJson::object();
    root["protocol"] = "rtsp";
    root["session_id"] = std::to_string(session.session_id);
    root["stream"] = StreamIdToJsonString(session.stream_id);
    root["transport"] = RtspTransportToJsonString(session.transport);
    root["remote_address"] = session.remote_address;
    root["local_address"] = session.local_address;
    root["subscription_id"] = session.subscription_id;
    root["subscription_open"] = session.subscription_open;
    root["subscription_generation"] = session.subscription_generation;
    root["subscription_pending_frames"] = session.subscription_pending_frames;
    root["subscription_waiting_keyframe"] = session.subscription_waiting_keyframe;
    root["subscription_slow"] = session.subscription_slow;
    root["subscription_close_reason"] = session.subscription_close_reason;
    root["pending_bytes"] = session.pending_bytes;
    root["rtp_packets"] = session.rtp_packets;
    root["rtp_bytes"] = session.rtp_bytes;
    root["rtcp_packets"] = session.rtcp_packets;
    root["rtcp_bytes"] = session.rtcp_bytes;
    root["last_rtcp_ms"] = session.last_rtcp_ms;
    root["close_reason"] = session.close_reason;
    return root;
}

const char *WebrtcPeerStateToJsonString(WebrtcPeerState state) {
    switch (state) {
        case WebrtcPeerState::kCreated:
            return "created";
        case WebrtcPeerState::kOfferReceived:
            return "offer_received";
        case WebrtcPeerState::kConnecting:
            return "connecting";
        case WebrtcPeerState::kConnected:
            return "connected";
        case WebrtcPeerState::kClosing:
            return "closing";
        case WebrtcPeerState::kClosed:
            return "closed";
        case WebrtcPeerState::kFailed:
            return "failed";
    }
    return "unknown";
}

ConfigJson WebrtcSessionToJson(const WebrtcPeerInfo &peer) {
    ConfigJson root = ConfigJson::object();
    root["protocol"] = "webrtc";
    root["session_id"] = peer.peer_id;
    root["peer_id"] = peer.peer_id;
    root["stream"] = StreamIdToJsonString(peer.stream_id);
    root["state"] = WebrtcPeerStateToJsonString(peer.state);
    root["client_ip"] = peer.client_ip;
    root["user_name"] = peer.user_name;
    root["subscription_id"] = peer.subscription_id;
    root["subscription_open"] = peer.subscription_open;
    root["subscription_generation"] = peer.subscription_generation;
    root["subscription_pending_frames"] = peer.subscription_pending_frames;
    root["subscription_waiting_keyframe"] = peer.subscription_waiting_keyframe;
    root["subscription_slow"] = peer.subscription_slow;
    root["subscription_close_reason"] = peer.subscription_close_reason;
    root["ice_selected"] = peer.ice_selected;
    root["dtls_state"] = peer.dtls_state;
    root["srtp_ready"] = peer.srtp_ready;
    root["rtp_packets"] = peer.rtp_packets;
    root["rtp_bytes"] = peer.rtp_bytes;
    root["rtcp_packets"] = peer.rtcp_packets;
    root["rtcp_bytes"] = peer.rtcp_bytes;
    root["rtcp_pli_count"] = peer.rtcp_pli_count;
    root["rtcp_fir_count"] = peer.rtcp_fir_count;
    root["rtcp_nack_count"] = peer.rtcp_nack_count;
    root["rtcp_transport_cc_count"] = peer.rtcp_transport_cc_count;
    root["rtcp_keyframe_requests"] = peer.rtcp_keyframe_requests;
    root["last_error"] = peer.last_error;
    root["created_at_ms"] = peer.created_at_ms;
    root["updated_at_ms"] = peer.updated_at_ms;
    return root;
}

void AddHttpStreamingMediaStatus(ConfigJson *root,
                                 MediaStreams *media_streams,
                                 StreamId stream_id) {
    if (root == nullptr || media_streams == nullptr) {
        return;
    }
    const MediaStreamInfo stream_info =
        media_streams->GetStreamInfo(stream_id);
    (*root)["media_running"] = stream_info.running;
    (*root)["media_track_ready"] = stream_info.track_ready;
    (*root)["media_codec"] = CodecToJsonString(stream_info.codec);
    (*root)["media_codec_generation"] = stream_info.codec_generation;
    (*root)["media_http_flv_ready"] = stream_info.flv_ready;
    (*root)["media_mjpeg_ready"] = stream_info.mjpeg_ready;
    (*root)["media_last_dts"] = stream_info.last_dts_us;
    (*root)["media_last_reset_reason"] = stream_info.last_reset_reason;
}

ConfigJson HttpStreamingSessionToJson(
    const HttpStreamSessionInfo &session,
    MediaStreams *media_streams) {
    ConfigJson root = ConfigJson::object();
    root["protocol"] = session.protocol;
    root["session_id"] = session.session_id;
    root["connection_id"] = session.connection_id;
    root["client_id"] = session.client_id;
    root["stream"] = StreamIdToJsonString(session.stream_id);
    root["stream_state"] = session.stream_state;
    if (session.open && session.stream_state == "opening") {
        root["state"] = "opening";
    } else if (session.open && session.stream_state == "closing") {
        root["state"] = "closing";
    } else {
        root["state"] = session.open ? "streaming" : "closed";
    }
    root["client_ip"] = session.client_ip;
    root["remote_address"] = session.remote_address;
    root["local_address"] = session.local_address;
    root["pending_bytes"] = session.pending_bytes;
    root["send_queue_length"] = session.send_queue_length;
    root["last_write_at_ms"] = session.last_write_at_ms;
    root["close_reason"] = session.close_reason;
    AddHttpStreamingMediaStatus(&root, media_streams, session.stream_id);
    return root;
}

bool IsMediaStreamingSession(const HttpStreamSessionInfo &session) {
    return session.protocol == "http_flv" || session.protocol == "mjpeg";
}

}  // namespace

class MediaHttpHandler : public IHttpHandler {
public:
    MediaHttpHandler(HttpAccess *access,
                     IConfig *config,
                     DeviceMedia *device,
                     MediaStreams *media_streams,
                     IRtsp *rtsp,
                     IWebrtc *webrtc,
                     IHttp *http)
        : access_(access),
          config_(config),
          device_(device),
          media_streams_(media_streams),
          rtsp_(rtsp),
          webrtc_(webrtc),
          http_(http) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/media/streams",
                              &MediaHttpHandler::HandleStreamsRoute, this);
        router->AddExactRoute(HttpMethod::kGet, "/api/media/capabilities",
                              &MediaHttpHandler::HandleCapabilitiesRoute,
                              this);
        router->AddPrefixRoute(HttpMethod::kGet, "/api/media/streams/",
                               &MediaHttpHandler::HandleStreamRoute, this);
        router->AddExactRoute(HttpMethod::kGet, "/api/media/sessions",
                              &MediaHttpHandler::HandleSessionsRoute, this);
    }

private:
    static HttpResponse HandleStreamsRoute(void *user,
                                           const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleStreams(request);
    }

    static HttpResponse HandleStreamRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleStream(request);
    }

    static HttpResponse HandleCapabilitiesRoute(void *user,
                                                const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleCapabilities(
            request);
    }

    static HttpResponse HandleSessionsRoute(void *user,
                                            const HttpRequest &request) {
        return static_cast<MediaHttpHandler *>(user)->HandleSessions(request);
    }

    bool RequireReadStatus(const HttpRequest &request,
                           AuthPrincipal *principal) {
        return access_ != nullptr &&
               access_->RequirePermission(request, AuthPermission::kReadStatus,
                                          "media", principal);
    }

    HttpResponse HandleStreams(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireReadStatus(request, &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        items.push_back(MediaStreamInfoToJson(StreamId::kMain, config_,
                                              device_,
                                              media_streams_, webrtc_));
        items.push_back(MediaStreamInfoToJson(StreamId::kSub, config_,
                                              device_,
                                              media_streams_, webrtc_));
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireReadStatus(request, &principal)) {
            return ForbiddenResponse(principal);
        }
        if (device_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        return JsonResponse(
            200, MediaCapabilitiesToJson(device_->GetCapabilities()));
    }

    HttpResponse HandleStream(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireReadStatus(request, &principal)) {
            return ForbiddenResponse(principal);
        }
        const std::string suffix =
            PathSuffix(request.path, "/api/media/streams/");
        const std::string url_suffix = "/urls";
        bool urls = false;
        std::string stream_name = suffix;
        if (suffix.size() > url_suffix.size() &&
            suffix.substr(suffix.size() - url_suffix.size()) == url_suffix) {
            urls = true;
            stream_name = suffix.substr(0, suffix.size() - url_suffix.size());
        }
        StreamId stream_id = StreamId::kMain;
        if (!StreamIdFromJsonString(stream_name, &stream_id)) {
            return ErrorResponse(404, HttpErrorCode::kStreamNotFound,
                                 "Stream not found");
        }
        if (urls) {
            return JsonResponse(
                200, PreviewUrlsToJson(config_, rtsp_, request, stream_id));
        }
        return JsonResponse(
            200, MediaStreamInfoToJson(stream_id, config_, device_,
                                       media_streams_, webrtc_));
    }

    HttpResponse HandleSessions(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireReadStatus(request, &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        if (rtsp_ != nullptr) {
            const std::vector<RtspSessionInfo> sessions =
                rtsp_->ListSessionInfo();
            for (const RtspSessionInfo &session : sessions) {
                items.push_back(RtspSessionToJson(session));
            }
        }
        if (http_ != nullptr) {
            const std::vector<HttpStreamSessionInfo> sessions =
                http_->ListStreamSessionInfo();
            for (const HttpStreamSessionInfo &session : sessions) {
                if (IsMediaStreamingSession(session)) {
                    items.push_back(HttpStreamingSessionToJson(
                        session, media_streams_));
                }
            }
        }
        WebrtcStats webrtc_stats;
        if (webrtc_ != nullptr) {
            webrtc_stats = webrtc_->GetStats();
            root["webrtc_active_peers"] = webrtc_stats.active_peers;
            root["webrtc_enabled"] = webrtc_stats.enabled;
            root["webrtc_signaling_ready"] = webrtc_stats.signaling_ready;
            root["webrtc_ice_ready"] = webrtc_stats.ice_ready;
            root["webrtc_dtls_ready"] = webrtc_stats.dtls_ready;
            root["webrtc_srtp_ready"] = webrtc_stats.srtp_ready;
            root["webrtc_public_ip"] = webrtc_stats.public_ip;
            root["webrtc_local_port_base"] = webrtc_stats.local_port_base;
            root["webrtc_max_peers"] = webrtc_stats.max_peers;
            root["webrtc_ice_server_count"] = webrtc_stats.ice_server_count;
            root["webrtc_selected_ice_pairs"] =
                webrtc_stats.selected_ice_pairs;
            const std::vector<WebrtcPeerInfo> peers = webrtc_->GetPeers();
            for (const WebrtcPeerInfo &peer : peers) {
                items.push_back(WebrtcSessionToJson(peer));
            }
        } else {
            root["webrtc_active_peers"] = 0;
            root["webrtc_enabled"] = false;
            root["webrtc_signaling_ready"] = false;
            root["webrtc_ice_ready"] = false;
            root["webrtc_dtls_ready"] = false;
            root["webrtc_srtp_ready"] = false;
            root["webrtc_public_ip"] = "";
            root["webrtc_local_port_base"] = 0;
            root["webrtc_max_peers"] = 0;
            root["webrtc_ice_server_count"] = 0;
            root["webrtc_selected_ice_pairs"] = 0;
        }
        MediaStreamStats media_stats;
        if (media_streams_ != nullptr) {
            media_stats = media_streams_->GetStreamStats();
        }
        root["http_flv_active_clients"] = media_stats.active_flv_clients;
        root["mjpeg_active_clients"] = media_stats.active_mjpeg_clients;
        root["rtsp_active_sessions"] =
            rtsp_ == nullptr ? 0 : rtsp_->GetStats().active_sessions;
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpAccess *access_ = nullptr;
    IConfig *config_ = nullptr;
    DeviceMedia *device_ = nullptr;
    MediaStreams *media_streams_ = nullptr;
    IRtsp *rtsp_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
    IHttp *http_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeMediaHandler(HttpAccess *access,
                                               IConfig *config,
                                               DeviceMedia *device,
                                               MediaStreams *media_streams,
                                               IRtsp *rtsp,
                                               IWebrtc *webrtc,
                                               IHttp *http) {
    return std::unique_ptr<IHttpHandler>(
        new MediaHttpHandler(access, config, device,
                             media_streams, rtsp, webrtc, http));
}

}  // namespace live_stream
