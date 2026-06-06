#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
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

HttpResponse HandleCreatePeer(IWebrtc *webrtc,
                              const HttpRequest &request,
                              const ConfigJson &body,
                              const AuthPrincipal &principal) {
    WebrtcCreatePeerRequest create_request;
    std::string stream;
    StreamId stream_id = StreamId::kMain;
    if (!json_utils::ReadField(body, "stream", &stream) ||
        !StreamIdFromJsonString(stream, &stream_id) ||
        !json_utils::ReadField(body, "client_id", &create_request.client_id)) {
        return StatusResponse(400, "Invalid stream");
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
        root["stream"] = StreamIdToJsonString(create_request.stream_id);
        root["state"] = WebrtcPeerStateName(WebrtcPeerState::kFailed);
        root["error"] = "create_peer_failed";
        return JsonResponse(200, root);
    }

    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    root["peer_id"] = peer.peer_id;
    root["stream"] = StreamIdToJsonString(peer.stream_id);
    root["state"] = WebrtcPeerStateName(peer.state);
    return JsonResponse(200, root);
}

HttpResponse HandleOffer(IWebrtc *webrtc,
                         const HttpRequest &request, const ConfigJson &body,
                         const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    WebrtcOfferRequest offer;
    if (!json_utils::ReadField(body, "peer_id", &offer.peer_id) ||
        !json_utils::ReadField(body, "sdp", &offer.sdp)) {
        return StatusResponse(400, "Missing offer fields");
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
        return JsonResponse(200, root);
    }

    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    root["peer_id"] = answer.peer_id;
    root["sdp"] = answer.sdp;
    root["state"] = WebrtcPeerStateName(answer.state);
    return JsonResponse(200, root);
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
        return StatusResponse(400, "Missing candidate fields");
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
        return StatusResponse(400, "Missing candidate fields");
    }
    if (!json_utils::ReadField(body, "username_fragment",
                          &candidate.username_fragment)) {
        (void)json_utils::ReadField(body, "usernameFragment",
                               &candidate.username_fragment);
    }

    Info(kHttpModuleName,
                   "WebRTC candidate peer=%s mid=%s index=%d size=%zu",
                   candidate.peer_id.c_str(), candidate.sdp_mid.c_str(),
                   candidate.sdp_mline_index, candidate.candidate.size());
    ConfigJson root = ConfigJson::object();
    root["peer_id"] = candidate.peer_id;
    if (webrtc->AddIceCandidate(candidate)) {
        root["ok"] = true;
        return JsonResponse(200, root);
    }
    root["ok"] = false;
    root["error"] = "peer_not_found";
    return JsonResponse(200, root);
}

HttpResponse HandleClosePeer(IWebrtc *webrtc,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    std::string peer_id;
    if (!json_utils::ReadField(body, "peer_id", &peer_id)) {
        return StatusResponse(400, "Missing peer_id");
    }
    ConfigJson root = ConfigJson::object();
    root["peer_id"] = peer_id;
    if (webrtc->ClosePeer(peer_id)) {
        root["ok"] = true;
        return JsonResponse(200, root);
    }
    root["ok"] = false;
    root["error"] = "peer_not_found";
    return JsonResponse(200, root);
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

    HttpResponse HandleWebrtc(const HttpRequest &request,
                              WebrtcRouteHandler handler) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (webrtc_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        if (IsMediaRestarting(device_media_)) {
            return StatusResponse(503, "Media pipeline restarting");
        }

        ConfigJson body;
        if (!ParseOptionalJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }

        return handler(webrtc_, request, body, principal);
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
