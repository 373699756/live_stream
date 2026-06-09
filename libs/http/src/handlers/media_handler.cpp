#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "config.h"
#include "http_protocol.h"
#include "json_utils.h"
#include "device_media.h"
#include "infra/log.h"
#include "media_source.h"
#include "rtsp.h"
#include "webrtc.h"

#include <cstdint>
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
    root["runtime_supported"] = capability.runtime_supported;
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
    root["runtime_supported"] = capability.runtime_supported;
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
        item["codec"] = VideoCodecToJsonString(codec.codec);
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

bool HasReadyBrowserProtocol(const MediaSourceStatus &status) {
    return status.hls_ready || status.flv_ready || status.mjpeg_ready;
}

void RequestBrowserRecoveryKeyFrame(IMediaSource *media_source,
                                    StreamId stream_id,
                                    const MediaSourceStatus &status) {
    if (media_source == nullptr || !status.running ||
        !status.browser_codec || HasReadyBrowserProtocol(status)) {
        return;
    }
    (void)media_source->RequestKeyFrame(stream_id,
                                              KeyFrameReason::kRecovery);
}

bool IsWebrtcReady(IWebrtc *webrtc) {
    if (webrtc == nullptr) {
        return false;
    }
    const WebrtcStats stats = webrtc->GetStats();
    return stats.enabled && stats.signaling_ready && stats.ice_ready &&
           stats.dtls_ready && stats.srtp_ready;
}

bool IsWebrtcSupported(VideoCodec codec, IWebrtc *webrtc) {
    if (webrtc == nullptr) {
        return false;
    }
    const WebrtcStats stats = webrtc->GetStats();
    return stats.enabled && (codec == VideoCodec::kH264 ||
                             codec == VideoCodec::kH265);
}

ConfigJson StreamRuntimeToJson(StreamId stream_id,
                               IDeviceMedia *device_media,
                               IMediaSource *media_source,
                               IWebrtc *webrtc) {
    MediaSourceStatus status;
    MediaSourceStats stats;
    bool media_source_available = false;
    if (media_source != nullptr) {
        status = media_source->GetBrowserStatus(stream_id);
        stats = media_source->GetStats();
        media_source_available = media_source->IsStreamAvailable(stream_id);
        RequestBrowserRecoveryKeyFrame(media_source, stream_id, status);
    }

    const bool device_stream_running =
        device_media != nullptr && device_media->IsStreamStarted(stream_id);
    const bool stream_running = status.running || device_stream_running;
    const VideoCodec codec =
        media_source != nullptr ? status.codec
                                : (device_media != nullptr
                                       ? device_media->GetStreamCodec(stream_id)
                                       : VideoCodec::kH264);

    ConfigJson root = ConfigJson::object();
    root["stream"] = StreamIdToJsonString(stream_id);
    root["available"] = media_source_available || device_media != nullptr;
    root["running"] = stream_running;
    root["codec"] = VideoCodecToJsonString(codec);
    root["codec_generation"] = status.codec_generation;
    root["track_ready"] = status.track_ready;
    root["hls_supported"] = status.hls_supported;
    root["hls_ready"] = status.hls_ready;
    root["http_flv_supported"] = status.flv_supported;
    root["http_flv_ready"] = status.flv_ready;
    root["mjpeg_supported"] = status.mjpeg_supported;
    root["mjpeg_ready"] = status.mjpeg_ready;
    const bool webrtc_supported = IsWebrtcSupported(codec, webrtc);
    root["webrtc_supported"] = webrtc_supported;
    root["webrtc_ready"] =
        stream_running && status.track_ready && webrtc_supported &&
        IsWebrtcReady(webrtc);
    root["reader_count"] = stats.active_frame_readers;
    root["client_count"] =
        stats.active_flv_clients + stats.active_mjpeg_clients;
    root["cached_frames"] = stats.cached_frames;
    root["cached_bytes"] = stats.cached_bytes;
    root["hls_bytes"] = status.hls_current_segment_size;
    root["last_dts"] = status.last_dts_us;
    root["last_keyframe_request_ms"] = status.last_keyframe_request_ms;
    root["last_keyframe_seen_ms"] = status.last_keyframe_seen_ms;
    root["last_first_frame_ms"] = status.last_first_frame_ms;
    root["last_protocol_ready_ms"] = status.last_protocol_ready_ms;
    root["last_reset_reason"] = status.last_reset_reason;
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
    ConfigJson network = config->GetValue("network");
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
    ConfigJson rtsp = config->GetValue("rtsp");
    int64_t port = 0;
    if (rtsp.is_object() &&
        json_utils::ReadField(rtsp, "port", &port, 1, 65535)) {
        return static_cast<uint16_t>(port);
    }
    ConfigJson network = config->GetValue("network");
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

ConfigJson PlaybackUrlsToJson(IConfig *config, IRtsp *rtsp,
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

ConfigJson RtspSessionToJson(const RtspSessionDiagnostics &session) {
    ConfigJson root = ConfigJson::object();
    root["protocol"] = "rtsp";
    root["session_id"] = std::to_string(session.session_id);
    root["stream"] = StreamIdToJsonString(session.stream_id);
    root["transport"] = RtspTransportToJsonString(session.transport);
    root["remote_address"] = session.remote_address;
    root["local_address"] = session.local_address;
    root["reader_id"] = session.reader_id;
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

}  // namespace

class MediaHttpHandler : public IHttpHandler {
public:
    MediaHttpHandler(HttpAccess *access,
                     IConfig *config,
                     IDeviceMedia *device_media,
                     IMediaSource *media_source,
                     IRtsp *rtsp,
                     IWebrtc *webrtc)
        : access_(access),
          config_(config),
          device_media_(device_media),
          media_source_(media_source),
          rtsp_(rtsp),
          webrtc_(webrtc) {}

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
        items.push_back(StreamRuntimeToJson(StreamId::kMain, device_media_,
                                            media_source_, webrtc_));
        items.push_back(StreamRuntimeToJson(StreamId::kSub, device_media_,
                                            media_source_, webrtc_));
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireReadStatus(request, &principal)) {
            return ForbiddenResponse(principal);
        }
        if (device_media_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        return JsonResponse(
            200, MediaCapabilitiesToJson(device_media_->GetCapabilities()));
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
                200, PlaybackUrlsToJson(config_, rtsp_, request, stream_id));
        }
        return JsonResponse(
            200, StreamRuntimeToJson(stream_id, device_media_,
                                     media_source_, webrtc_));
    }

    HttpResponse HandleSessions(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireReadStatus(request, &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        if (rtsp_ != nullptr) {
            const std::vector<RtspSessionDiagnostics> sessions =
                rtsp_->GetSessionDiagnostics();
            for (const RtspSessionDiagnostics &session : sessions) {
                items.push_back(RtspSessionToJson(session));
            }
        }
        WebrtcStats webrtc_stats;
        if (webrtc_ != nullptr) {
            webrtc_stats = webrtc_->GetStats();
            root["webrtc_active_peers"] = webrtc_stats.active_peers;
            const std::vector<WebrtcPeerInfo> peers = webrtc_->GetPeers();
            for (const WebrtcPeerInfo &peer : peers) {
                items.push_back(WebrtcSessionToJson(peer));
            }
        } else {
            root["webrtc_active_peers"] = 0;
        }
        MediaSourceStats media_stats;
        if (media_source_ != nullptr) {
            media_stats = media_source_->GetStats();
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
    IDeviceMedia *device_media_ = nullptr;
    IMediaSource *media_source_ = nullptr;
    IRtsp *rtsp_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeMediaHandler(HttpAccess *access,
                                            IConfig *config,
                                            IDeviceMedia *device_media,
                                            IMediaSource *media_source,
                                            IRtsp *rtsp,
                                            IWebrtc *webrtc) {
    return std::unique_ptr<IHttpHandler>(
        new MediaHttpHandler(access, config, device_media,
                             media_source, rtsp, webrtc));
}

}  // namespace live_stream
