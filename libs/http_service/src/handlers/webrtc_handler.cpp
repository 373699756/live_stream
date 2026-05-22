#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
#include "json_utils.h"
#include "webrtc_service.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace {

using WebrtcRouteHandler =
    HttpResponse (*)(IWebrtcService *webrtc_service,
                     const HttpRequest &request,
                     const ConfigJson &body,
                     const AuthPrincipal &principal);

HttpResponse HandleCreatePeer(IWebrtcService *webrtc_service,
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

    const WebrtcPeerInfo peer = webrtc_service->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        return StatusResponse(409, "Could not create peer");
    }

    ConfigJson root = ConfigJson::object();
    root["peer_id"] = peer.peer_id;
    root["stream"] = StreamIdToJsonString(peer.stream_id);
    return JsonResponse(200, root);
}

HttpResponse HandleOffer(IWebrtcService *webrtc_service,
                         const HttpRequest &request, const ConfigJson &body,
                         const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    WebrtcOfferRequest offer;
    if (!json_utils::ReadField(body, "peer_id", &offer.peer_id) ||
        !json_utils::ReadField(body, "sdp", &offer.sdp)) {
        return StatusResponse(400, "Missing offer fields");
    }

    const WebrtcAnswer answer = webrtc_service->HandleOffer(offer);
    if (answer.sdp.empty()) {
        return StatusResponse(404, "Peer not found");
    }

    ConfigJson root = ConfigJson::object();
    root["peer_id"] = answer.peer_id;
    root["sdp"] = answer.sdp;
    return JsonResponse(200, root);
}

HttpResponse HandleCandidate(IWebrtcService *webrtc_service,
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

    INFRA_LOG_INFO(kHttpModuleName,
                   "WebRTC candidate peer=%s mid=%s index=%d size=%zu",
                   candidate.peer_id.c_str(), candidate.sdp_mid.c_str(),
                   candidate.sdp_mline_index, candidate.candidate.size());
    return webrtc_service->AddIceCandidate(candidate)
               ? OkResponse()
               : StatusResponse(404, "Peer not found");
}

HttpResponse HandleClosePeer(IWebrtcService *webrtc_service,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    std::string peer_id;
    if (!json_utils::ReadField(body, "peer_id", &peer_id)) {
        return StatusResponse(400, "Missing peer_id");
    }
    return webrtc_service->ClosePeer(peer_id)
               ? OkResponse()
               : StatusResponse(404, "Peer not found");
}

}  // namespace

class WebrtcHttpHandler : public IHttpHandler {
public:
    WebrtcHttpHandler(HttpAccess *access,
                      IMediaService *media_service,
                      IWebrtcService *webrtc_service)
        : access_(access), media_service_(media_service),
          webrtc_service_(webrtc_service) {}

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
        if (!RequireAuth(access_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        if (webrtc_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        if (IsMediaRestarting(media_service_)) {
            return StatusResponse(503, "Media pipeline restarting");
        }

        ConfigJson body;
        if (!ParseOptionalJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }

        return handler(webrtc_service_, request, body, principal);
    }

    HttpAccess *access_ = nullptr;
    IMediaService *media_service_ = nullptr;
    IWebrtcService *webrtc_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateWebrtcHttpHandler(HttpAccess *access,
                        IMediaService *media_service,
                        IWebrtcService *webrtc_service) {
    return std::unique_ptr<IHttpHandler>(
        new WebrtcHttpHandler(access, media_service, webrtc_service));
}

}  // namespace live_stream
