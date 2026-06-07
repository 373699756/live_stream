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
        return HttpMediaStatusResponse(400, "Invalid stream");
    }
    create_request.stream_id = stream_id;
    create_request.session_id = principal.session_id;
    create_request.user_name = principal.user_name;
    create_request.client_ip = request.client_ip;

    const WebrtcPeerInfo peer = webrtc->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        ConfigJson root = ConfigJson::object();
        root["ok"] = false;
        root["peer_id"] = "";
        root["stream"] = HttpMediaStreamIdToJsonString(create_request.stream_id);
        root["state"] = WebrtcPeerStateName(WebrtcPeerState::kFailed);
        root["error"] = "create_peer_failed";
        return HttpMediaJsonResponse(200, root);
    }

    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    root["peer_id"] = peer.peer_id;
    root["stream"] = HttpMediaStreamIdToJsonString(peer.stream_id);
    root["state"] = WebrtcPeerStateName(peer.state);
    return HttpMediaJsonResponse(200, root);
}

HttpResponse HandleOffer(IWebrtc *webrtc,
                         const HttpRequest &request, const ConfigJson &body,
                         const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    WebrtcOfferRequest offer;
    if (!json_utils::ReadField(body, "peer_id", &offer.peer_id) ||
        !json_utils::ReadField(body, "sdp", &offer.sdp)) {
        return HttpMediaStatusResponse(400, "Missing offer fields");
    }

    const WebrtcAnswer answer = webrtc->HandleOffer(offer);
    if (answer.sdp.empty()) {
        ConfigJson root = ConfigJson::object();
        root["ok"] = false;
        root["peer_id"] = answer.peer_id;
        root["state"] = WebrtcPeerStateName(answer.state);
        root["error"] =
            answer.error.empty() ? std::string("answer_unavailable")
                                 : answer.error;
        return HttpMediaJsonResponse(200, root);
    }

    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    root["peer_id"] = answer.peer_id;
    root["sdp"] = answer.sdp;
    root["state"] = WebrtcPeerStateName(answer.state);
    return HttpMediaJsonResponse(200, root);
}

HttpResponse HandleCandidate(IWebrtc *webrtc,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    WebrtcIceCandidate candidate;
    bool has_mline_index = false;
    if (!json_utils::ReadField(body, "peer_id", &candidate.peer_id) ||
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
        root["ok"] = true;
        return HttpMediaJsonResponse(200, root);
    }
    root["ok"] = false;
    root["error"] = "peer_not_found";
    return HttpMediaJsonResponse(200, root);
}

HttpResponse HandleClosePeer(IWebrtc *webrtc,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    std::string peer_id;
    if (!json_utils::ReadField(body, "peer_id", &peer_id)) {
        return HttpMediaStatusResponse(400, "Missing peer_id");
    }
    ConfigJson root = ConfigJson::object();
    root["peer_id"] = peer_id;
    if (webrtc->ClosePeer(peer_id)) {
        root["ok"] = true;
        return HttpMediaJsonResponse(200, root);
    }
    root["ok"] = false;
    root["error"] = "peer_not_found";
    return HttpMediaJsonResponse(200, root);
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
        router->AddExactRoute(HttpMethod::kPost, "/api/webrtc/offer",
                              &WebrtcHttpHandler::HandleOfferRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/webrtc/candidate",
                              &WebrtcHttpHandler::HandleCandidateRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/webrtc/close",
                              &WebrtcHttpHandler::HandleClosePeerRoute, this);
        router->AddExactRoute(HttpMethod::kDelete, "/api/webrtc/close",
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

    static HttpResponse HandleOfferRoute(void *user,
                                         const HttpRequest &request) {
        return static_cast<WebrtcHttpHandler *>(user)->HandleWebrtc(
            request, &HandleOffer);
    }

    static HttpResponse HandleCandidateRoute(void *user,
                                             const HttpRequest &request) {
        return static_cast<WebrtcHttpHandler *>(user)->HandleWebrtc(
            request, &HandleCandidate);
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
            return HttpMediaStatusResponse(501, "Not Implemented");
        }
        if (IsHttpMediaRestarting(device_media_)) {
            return HttpMediaStatusResponse(503, "Media pipeline restarting");
        }

        ConfigJson body;
        if (!ParseHttpMediaOptionalJsonObject(request, &body)) {
            return HttpMediaStatusResponse(400, "Invalid JSON");
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
