#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
#include "live_stream/json_utils.h"
#include "webrtc_service.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace {

using WebrtcRouteHandler =
    HttpResponse (*)(HttpHandlerContext *context, const HttpRequest &request,
                     const ConfigJson &body,
                     const AuthPrincipal &principal);

struct WebrtcRoute {
    HttpMethod method = HttpMethod::kPost;
    const char *path = nullptr;
    WebrtcRouteHandler handler = nullptr;
};

HttpResponse HandleCreatePeer(HttpHandlerContext *context,
                              const HttpRequest &request,
                              const ConfigJson &body,
                              const AuthPrincipal &principal) {
    WebrtcCreatePeerRequest create_request;
    std::string stream;
    StreamId stream_id = StreamId::kMain;
    if (!json_utils::Load(body, "stream", &stream) ||
        !StreamIdFromJsonString(stream, &stream_id) ||
        !json_utils::Load(body, "client_id", &create_request.client_id)) {
        return StatusResponse(400, "Invalid stream");
    }
    create_request.stream_id = stream_id;
    create_request.session_id = principal.session_id;
    create_request.user_name = principal.user_name;
    create_request.client_ip = request.client_ip;

    const WebrtcPeerInfo peer =
        context->Dependencies().webrtc_service->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        return StatusResponse(409, "Could not create peer");
    }

    ConfigJson root = ConfigJson::object();
    root["peer_id"] = peer.peer_id;
    root["stream"] = StreamIdToJsonString(peer.stream_id);
    return JsonResponse(200, root);
}

HttpResponse HandleOffer(HttpHandlerContext *context,
                         const HttpRequest &request, const ConfigJson &body,
                         const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    WebrtcOfferRequest offer;
    if (!json_utils::Load(body, "peer_id", &offer.peer_id) ||
        !json_utils::Load(body, "sdp", &offer.sdp)) {
        return StatusResponse(400, "Missing offer fields");
    }

    const WebrtcAnswer answer =
        context->Dependencies().webrtc_service->HandleOffer(offer);
    if (answer.sdp.empty()) {
        return StatusResponse(404, "Peer not found");
    }

    ConfigJson root = ConfigJson::object();
    root["peer_id"] = answer.peer_id;
    root["sdp"] = answer.sdp;
    return JsonResponse(200, root);
}

HttpResponse HandleCandidate(HttpHandlerContext *context,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    WebrtcIceCandidate candidate;
    bool has_mline_index = false;
    if (!json_utils::Load(body, "peer_id", &candidate.peer_id) ||
        !json_utils::Load(body, "candidate", &candidate.candidate)) {
        return StatusResponse(400, "Missing candidate fields");
    }
    if (!json_utils::Load(body, "sdp_mid", &candidate.sdp_mid)) {
        (void)json_utils::Load(body, "sdpMid", &candidate.sdp_mid);
    }
    has_mline_index =
        json_utils::Load(body, "sdp_mline_index",
                         &candidate.sdp_mline_index, 0,
                         std::numeric_limits<int32_t>::max()) ||
        json_utils::Load(body, "sdpMLineIndex",
                         &candidate.sdp_mline_index, 0,
                         std::numeric_limits<int32_t>::max());
    if (!has_mline_index) {
        return StatusResponse(400, "Missing candidate fields");
    }
    if (!json_utils::Load(body, "username_fragment",
                          &candidate.username_fragment)) {
        (void)json_utils::Load(body, "usernameFragment",
                               &candidate.username_fragment);
    }

    INFRA_LOG_INFO(kHttpModuleName,
                   "WebRTC candidate peer=%s mid=%s index=%d size=%zu",
                   candidate.peer_id.c_str(), candidate.sdp_mid.c_str(),
                   candidate.sdp_mline_index, candidate.candidate.size());
    return context->Dependencies().webrtc_service->AddIceCandidate(candidate)
               ? OkResponse()
               : StatusResponse(404, "Peer not found");
}

HttpResponse HandleClosePeer(HttpHandlerContext *context,
                             const HttpRequest &request,
                             const ConfigJson &body,
                             const AuthPrincipal &principal) {
    (void)request;
    (void)principal;
    std::string peer_id;
    if (!json_utils::Load(body, "peer_id", &peer_id)) {
        return StatusResponse(400, "Missing peer_id");
    }
    return context->Dependencies().webrtc_service->ClosePeer(peer_id)
               ? OkResponse()
               : StatusResponse(404, "Peer not found");
}

const WebrtcRoute *FindWebrtcRoute(const HttpRequest &request) {
    static const WebrtcRoute kRoutes[] = {
        {HttpMethod::kPost, "/api/webrtc/peers", HandleCreatePeer},
        {HttpMethod::kPost, "/api/webrtc/offer", HandleOffer},
        {HttpMethod::kPost, "/api/webrtc/candidate", HandleCandidate},
        {HttpMethod::kPost, "/api/webrtc/close", HandleClosePeer},
        {HttpMethod::kDelete, "/api/webrtc/close", HandleClosePeer},
    };

    for (const WebrtcRoute &route : kRoutes) {
        if (route.method == request.method && request.path == route.path) {
            return &route;
        }
    }
    return nullptr;
}

}  // namespace

HttpResponse http_handlers::HandleWebrtc(HttpHandlerContext *context,
                                         const HttpRequest &request) {
    AuthPrincipal principal;
    if (!RequireAuth(context, request, &principal)) {
        return StatusResponse(401, "Unauthorized");
    }
    if (context->Dependencies().webrtc_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    if (IsMediaRestarting(context)) {
        return StatusResponse(503, "Media pipeline restarting");
    }

    ConfigJson body;
    if (!ParseOptionalJsonObject(request, &body)) {
        return StatusResponse(400, "Invalid JSON");
    }

    const WebrtcRoute *route = FindWebrtcRoute(request);
    if (route == nullptr) {
        return StatusResponse(404, "Not Found");
    }
    return route->handler(context, request, body, principal);
}

}  // namespace live_stream
