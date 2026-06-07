#include "http_media.h"

#include "http_media_utils.h"
#include "http_router.h"

#include "device_media.h"
#include "json_utils.h"
#include "webrtc.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace {

using WebrtcRouteHandler =
    HttpResponse (*)(IWebrtc *webrtc,
                     const HttpRequest &request,
                     const ConfigJson &body,
                     const AuthPrincipal &principal);

const char *WebrtcPeerStateName(WebrtcPeerState state) {
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

ConfigJson WebrtcPeerInfoToJson(const WebrtcPeerInfo &peer) {
    ConfigJson root = ConfigJson::object();
    root["peer_id"] = peer.peer_id;
    root["stream"] = HttpMediaStreamIdToJsonString(peer.stream_id);
    root["codec"] = VideoCodecToJsonString(peer.codec);
    root["state"] = WebrtcPeerStateName(peer.state);
    root["client_id"] = peer.client_id;
    root["ice_selected"] = peer.ice_selected;
    root["dtls_state"] = peer.dtls_state;
    root["srtp_ready"] = peer.srtp_ready;
    root["rtp_packets"] = peer.rtp_packets;
    root["rtp_bytes"] = peer.rtp_bytes;
    root["last_error"] = peer.last_error;
    root["created_at_ms"] = peer.created_at_ms;
    root["updated_at_ms"] = peer.updated_at_ms;
    return root;
}

HttpResponse WebrtcErrorResponse(int status_code, const std::string &code,
                                 const std::string &message) {
    ConfigJson root = ConfigJson::object();
    ConfigJson error = ConfigJson::object();
    error["code"] = code;
    error["message"] = message;
    root["error"] = error;
    return HttpMediaJsonResponse(status_code, root);
}

bool ParsePeerSubPath(const HttpRequest &request, const std::string &suffix,
                      std::string *peer_id) {
    const std::string prefix = "/api/webrtc/peers/";
    std::string path_suffix = HttpMediaPathSuffix(request.path, prefix);
    if (path_suffix.size() <= suffix.size() ||
        path_suffix.substr(path_suffix.size() - suffix.size()) != suffix) {
        return false;
    }
    const std::string parsed_peer_id =
        path_suffix.substr(0, path_suffix.size() - suffix.size());
    if (parsed_peer_id.empty() ||
        parsed_peer_id.find('/') != std::string::npos) {
        return false;
    }
    if (peer_id != nullptr) {
        *peer_id = parsed_peer_id;
    }
    return true;
}

bool ParsePeerPath(const HttpRequest &request, std::string *peer_id) {
    if (peer_id == nullptr) {
        return false;
    }
    const std::string prefix = "/api/webrtc/peers/";
    *peer_id = HttpMediaPathSuffix(request.path, prefix);
    return !peer_id->empty() && peer_id->find('/') == std::string::npos;
}

std::string RequiredWhepPrefix(StreamId stream_id) {
    return std::string("/live/") + HttpMediaStreamIdToJsonString(stream_id) +
           "/whep";
}

bool ParseWhepPath(const HttpRequest &request, StreamId *stream_id,
                   std::string *peer_id) {
    if (stream_id == nullptr || peer_id == nullptr) {
        return false;
    }
    const std::string remaining = HttpMediaPathSuffix(request.path, "/live/");
    const size_t slash = remaining.find('/');
    if (slash == std::string::npos || slash == 0) {
        return false;
    }
    const std::string stream_name = remaining.substr(0, slash);
    if (!HttpMediaStreamIdFromJsonString(stream_name, stream_id)) {
        return false;
    }
    const std::string whep_path = remaining.substr(slash + 1);
    if (whep_path == "whep") {
        peer_id->clear();
        return true;
    }
    const std::string prefix = "whep/";
    if (!HttpMediaStartsWith(whep_path, prefix) ||
        whep_path.size() <= prefix.size()) {
        return false;
    }
    *peer_id = whep_path.substr(prefix.size());
    return !peer_id->empty();
}

HttpResponse HandleCreatePeer(IWebrtc *webrtc,
                              const HttpRequest &request,
                              const ConfigJson &body,
                              const AuthPrincipal &principal) {
    WebrtcCreatePeerRequest create_request;
    std::string stream;
    StreamId stream_id = StreamId::kMain;
    if (!json_utils::ReadField(body, "stream", &stream) ||
        !HttpMediaStreamIdFromJsonString(stream, &stream_id) ||
        !json_utils::ReadField(body, "client_id", &create_request.client_id)) {
        return WebrtcErrorResponse(400, "stream_not_found",
                                   "Stream not found");
    }
    create_request.stream_id = stream_id;
    create_request.session_id = principal.session_id;
    create_request.user_name = principal.user_name;
    create_request.client_ip = request.client_ip;

    const WebrtcPeerInfo peer = webrtc->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        return WebrtcErrorResponse(503, "protocol_unavailable",
                                   "Could not create WebRTC peer");
    }

    return HttpMediaJsonResponse(200, WebrtcPeerInfoToJson(peer));
}

HttpResponse HandleOffer(IWebrtc *webrtc,
                         const HttpRequest &request, const ConfigJson &body,
                         const AuthPrincipal &principal) {
    (void)principal;
    WebrtcOfferRequest offer;
    if (!ParsePeerSubPath(request, "/offer", &offer.peer_id) ||
        !json_utils::ReadField(body, "sdp", &offer.sdp)) {
        return HttpMediaStatusResponse(400, "Missing offer fields");
    }

    const WebrtcAnswer answer = webrtc->HandleOffer(offer);
    if (answer.sdp.empty()) {
        if (answer.error == "peer_not_found") {
            return WebrtcErrorResponse(404, "peer_not_found",
                                       "WebRTC peer not found");
        }
        return WebrtcErrorResponse(
            503, "protocol_unavailable",
            answer.error.empty() ? "WebRTC answer unavailable" : answer.error);
    }

    ConfigJson root = ConfigJson::object();
    root["peer_id"] = answer.peer_id;
    root["sdp"] = answer.sdp;
    root["state"] = WebrtcPeerStateName(answer.state);
    return HttpMediaJsonResponse(200, root);
}

HttpResponse HandleCandidate(IWebrtc *webrtc,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)principal;
    WebrtcIceCandidate candidate;
    bool has_mline_index = false;
    if (!ParsePeerSubPath(request, "/candidates", &candidate.peer_id) ||
        !json_utils::ReadField(body, "candidate", &candidate.candidate)) {
        return HttpMediaStatusResponse(400, "Missing candidate fields");
    }
    if (!json_utils::ReadField(body, "sdp_mid", &candidate.sdp_mid)) {
        (void)json_utils::ReadField(body, "sdpMid", &candidate.sdp_mid);
    }
    has_mline_index =
        json_utils::ReadField(body, "sdp_mline_index",
                         &candidate.sdp_mline_index, 0,
                         std::numeric_limits<int32_t>::max()) ||
        json_utils::ReadField(body, "sdpMLineIndex",
                         &candidate.sdp_mline_index, 0,
                         std::numeric_limits<int32_t>::max());
    if (!has_mline_index) {
        return HttpMediaStatusResponse(400, "Missing candidate fields");
    }
    if (!json_utils::ReadField(body, "username_fragment",
                          &candidate.username_fragment)) {
        (void)json_utils::ReadField(body, "usernameFragment",
                               &candidate.username_fragment);
    }

    ConfigJson root = ConfigJson::object();
    root["peer_id"] = candidate.peer_id;
    if (webrtc->AddIceCandidate(candidate)) {
        return HttpMediaJsonResponse(200, root);
    }
    return WebrtcErrorResponse(404, "peer_not_found",
                               "WebRTC peer not found");
}

HttpResponse HandleClosePeer(IWebrtc *webrtc,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)body;
    (void)principal;
    std::string peer_id;
    if (!ParsePeerPath(request, &peer_id)) {
        return HttpMediaStatusResponse(400, "Missing peer_id");
    }
    ConfigJson root = ConfigJson::object();
    root["peer_id"] = peer_id;
    if (webrtc->ClosePeer(peer_id)) {
        root["state"] = WebrtcPeerStateName(WebrtcPeerState::kClosed);
        return HttpMediaJsonResponse(200, root);
    }
    return WebrtcErrorResponse(404, "peer_not_found",
                               "WebRTC peer not found");
}

HttpResponse BuildWhepCreateResponse(IWebrtc *webrtc,
                                     const HttpRequest &request,
                                     const AuthPrincipal &principal) {
    StreamId stream_id = StreamId::kMain;
    std::string path_peer_id;
    if (!ParseWhepPath(request, &stream_id, &path_peer_id) ||
        !path_peer_id.empty()) {
        return HttpMediaTextResponse(404, "Not Found");
    }
    if (request.body.empty()) {
        return HttpMediaTextResponse(400, "Missing SDP offer");
    }

    WebrtcCreatePeerRequest create_request;
    create_request.stream_id = stream_id;
    create_request.client_id = "whep";
    create_request.session_id = principal.session_id;
    create_request.user_name = principal.user_name;
    create_request.client_ip = request.client_ip;
    const WebrtcPeerInfo peer = webrtc->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        return HttpMediaTextResponse(503, "Could not create WHEP peer");
    }

    WebrtcOfferRequest offer;
    offer.peer_id = peer.peer_id;
    offer.sdp = request.body;
    const WebrtcAnswer answer = webrtc->HandleOffer(offer);
    if (answer.sdp.empty()) {
        (void)webrtc->ClosePeer(peer.peer_id);
        return HttpMediaTextResponse(
            503, answer.error.empty() ? "Could not create WHEP answer"
                                      : answer.error);
    }

    HttpResponse response;
    response.status_code = 201;
    response.headers["Content-Type"] = "application/sdp";
    response.headers["Location"] =
        RequiredWhepPrefix(stream_id) + "/" + peer.peer_id;
    response.body = answer.sdp;
    return response;
}

HttpResponse BuildWhepDeleteResponse(IWebrtc *webrtc,
                                     const HttpRequest &request) {
    StreamId stream_id = StreamId::kMain;
    std::string peer_id;
    if (!ParseWhepPath(request, &stream_id, &peer_id) || peer_id.empty()) {
        return HttpMediaTextResponse(404, "Not Found");
    }
    (void)stream_id;
    if (!webrtc->ClosePeer(peer_id)) {
        return HttpMediaTextResponse(404, "WHEP peer not found");
    }
    return HttpMediaTextResponse(204, std::string());
}

}  // namespace

class WebrtcHttpHandler : public IHttpHandler {
public:
    WebrtcHttpHandler(HttpAccess *access,
                      IDeviceMedia *device_media,
                      IWebrtc *webrtc)
        : access_(access), device_media_(device_media),
          webrtc_(webrtc) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kPost, "/api/webrtc/peers",
                              &WebrtcHttpHandler::HandleCreatePeerRoute,
                              this);
        router->AddPrefixRoute(HttpMethod::kPost, "/api/webrtc/peers/",
                               &WebrtcHttpHandler::HandlePeerPostRoute, this);
        router->AddPrefixRoute(HttpMethod::kDelete, "/api/webrtc/peers/",
                               &WebrtcHttpHandler::HandleClosePeerRoute, this);
        router->AddPrefixRoute(HttpMethod::kPost, "/live/",
                              &WebrtcHttpHandler::HandleWhepCreateRoute, this);
        router->AddPrefixRoute(HttpMethod::kDelete, "/live/",
                              &WebrtcHttpHandler::HandleWhepDeleteRoute, this);
    }

private:
    static HttpResponse HandleCreatePeerRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<WebrtcHttpHandler *>(user)->HandleWebrtc(
            request, &HandleCreatePeer);
    }

    static HttpResponse HandlePeerPostRoute(void *user,
                                            const HttpRequest &request) {
        if (ParsePeerSubPath(request, "/offer", nullptr)) {
            return static_cast<WebrtcHttpHandler *>(user)->HandleWebrtc(
                request, &HandleOffer);
        }
        if (ParsePeerSubPath(request, "/candidates", nullptr)) {
            return static_cast<WebrtcHttpHandler *>(user)->HandleWebrtc(
                request, &HandleCandidate);
        }
        return WebrtcErrorResponse(404, "invalid_argument", "Not Found");
    }

    static HttpResponse HandleClosePeerRoute(void *user,
                                             const HttpRequest &request) {
        return static_cast<WebrtcHttpHandler *>(user)->HandleWebrtc(
            request, &HandleClosePeer);
    }

    static HttpResponse HandleWhepCreateRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<WebrtcHttpHandler *>(user)->HandleWhepCreate(
            request);
    }

    static HttpResponse HandleWhepDeleteRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<WebrtcHttpHandler *>(user)->HandleWhepDelete(
            request);
    }

    HttpResponse HandleWebrtc(const HttpRequest &request,
                              WebrtcRouteHandler handler) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (webrtc_ == nullptr) {
            return WebrtcErrorResponse(501, "protocol_unavailable",
                                       "Not Implemented");
        }
        if (IsHttpMediaRestarting(device_media_)) {
            return WebrtcErrorResponse(503, "resource_busy",
                                       "Media pipeline restarting");
        }

        ConfigJson body;
        if (!ParseHttpMediaOptionalJsonObject(request, &body)) {
            return WebrtcErrorResponse(400, "invalid_argument",
                                       "Invalid JSON");
        }

        return handler(webrtc_, request, body, principal);
    }

    HttpResponse HandleWhepCreate(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaPlaybackAuthResponse(access_, request,
                                                 &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (webrtc_ == nullptr) {
            return HttpMediaTextResponse(501, "Not Implemented");
        }
        if (IsHttpMediaRestarting(device_media_)) {
            return HttpMediaTextResponse(503, "Media pipeline restarting");
        }
        return BuildWhepCreateResponse(webrtc_, request, principal);
    }

    HttpResponse HandleWhepDelete(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaPlaybackAuthResponse(access_, request,
                                                 &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (webrtc_ == nullptr) {
            return HttpMediaTextResponse(501, "Not Implemented");
        }
        return BuildWhepDeleteResponse(webrtc_, request);
    }

    HttpAccess *access_ = nullptr;
    IDeviceMedia *device_media_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateWebrtcHttpHandler(HttpAccess *access,
                        IDeviceMedia *device_media,
                        IWebrtc *webrtc) {
    return std::unique_ptr<IHttpHandler>(
        new WebrtcHttpHandler(access, device_media, webrtc));
}

}  // namespace live_stream
