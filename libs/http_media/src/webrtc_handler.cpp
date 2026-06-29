#include "http_media.h"

#include "http_media_auth.h"
#include "http_media_json_body.h"
#include "http_media_path.h"
#include "http_media_response.h"
#include "http_media_stream_id_json.h"
#include "http_router.h"

#include "device.h"
#include "json_reader.h"
#include "webrtc.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace {

using WebrtcRouteHandler =
    HttpResponse (*)(IWebrtc *webrtc,
                     const HttpRequest &request,
                     const Json &body,
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

Json WebrtcPeerInfoToJson(const WebrtcPeerInfo &peer) {
    Json root = Json::object();
    root["peer_id"] = peer.peer_id;
    root["stream"] = MediaStreamIdToJson(peer.stream_id);
    root["codec"] = CodecToJsonString(peer.codec);
    root["state"] = WebrtcPeerStateName(peer.state);
    root["client_id"] = peer.client_id;
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
    root["rtcp_pli_packets"] = peer.rtcp_pli_packets;
    root["rtcp_fir_packets"] = peer.rtcp_fir_packets;
    root["rtcp_nack_packets"] = peer.rtcp_nack_packets;
    root["rtcp_transport_cc_packets"] = peer.rtcp_transport_cc_packets;
    root["rtcp_keyframe_requests"] = peer.rtcp_keyframe_requests;
    root["last_error"] = peer.last_error;
    root["created_at_ms"] = peer.created_at_ms;
    root["updated_at_ms"] = peer.updated_at_ms;
    return root;
}

HttpResponse WebrtcErrorResponse(int status_code, const std::string &code,
                                 const std::string &msg) {
    Json root = Json::object();
    Json error = Json::object();
    error["code"] = code;
    error["message"] = msg;
    root["error"] = error;
    return HttpMediaJsonResponse(status_code, root);
}

HttpResponse WebrtcCreatePeerErrorResponse(const std::string &last_error) {
    const std::string peer_error =
        last_error.empty() ? "peer_create_failed" : last_error;
    if (peer_error == "invalid_stream") {
        return WebrtcErrorResponse(400, "stream_not_found",
                                   "WebRTC stream not found");
    }
    if (peer_error == "stream_unavailable" ||
        peer_error == "unsupported_codec") {
        return WebrtcErrorResponse(409, "resource_busy", peer_error);
    }
    if (peer_error == "peer_limit_reached") {
        return WebrtcErrorResponse(409, "resource_busy",
                                   "WebRTC peer limit reached");
    }
    return WebrtcErrorResponse(503, "protocol_unavailable", peer_error);
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
    return std::string("/live/") + MediaStreamIdToJson(stream_id) +
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
    if (!MediaStreamIdFromJson(stream_name, stream_id)) {
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

HttpResponse BuildCreatePeerResponse(IWebrtc *webrtc,
                                     const HttpRequest &request,
                                     const Json &body,
                                     const AuthPrincipal &principal) {
    WebrtcCreatePeerRequest create_request;
    std::string stream;
    StreamId stream_id = StreamId::kMain;
    if (!json_reader::ReadField(body, "stream", &stream) ||
        !MediaStreamIdFromJson(stream, &stream_id) ||
        !json_reader::ReadField(body, "client_id", &create_request.client_id)) {
        return WebrtcErrorResponse(400, "stream_not_found",
                                   "Stream not found");
    }
    create_request.stream_id = stream_id;
    create_request.session_id = principal.session_id;
    create_request.user_name = principal.user_name;
    create_request.client_ip = request.client_ip;

    const WebrtcPeerInfo peer = webrtc->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        return WebrtcCreatePeerErrorResponse(peer.last_error);
    }

    return HttpMediaJsonResponse(200, WebrtcPeerInfoToJson(peer));
}

HttpResponse BuildOfferResponse(IWebrtc *webrtc,
                                const HttpRequest &request, const Json &body,
                                const AuthPrincipal &principal) {
    (void)principal;
    WebrtcOfferRequest offer;
    if (!ParsePeerSubPath(request, "/offer", &offer.peer_id) ||
        !json_reader::ReadField(body, "sdp", &offer.sdp)) {
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

    Json root = Json::object();
    root["peer_id"] = answer.peer_id;
    root["sdp"] = answer.sdp;
    root["state"] = WebrtcPeerStateName(answer.state);
    return HttpMediaJsonResponse(200, root);
}

HttpResponse BuildCandidateResponse(IWebrtc *webrtc,
                                    const HttpRequest &request,
                                    const Json &body,
                                    const AuthPrincipal &principal) {
    (void)principal;
    WebrtcIceCandidate candidate;
    bool has_mline_index = false;
    if (!ParsePeerSubPath(request, "/candidates", &candidate.peer_id) ||
        !json_reader::ReadField(body, "candidate", &candidate.candidate)) {
        return HttpMediaStatusResponse(400, "Missing candidate fields");
    }
    if (!json_reader::ReadField(body, "sdp_mid", &candidate.sdp_mid)) {
        (void)json_reader::ReadField(body, "sdpMid", &candidate.sdp_mid);
    }
    has_mline_index =
        json_reader::ReadField(body, "sdp_mline_index",
                               &candidate.sdp_mline_index, 0,
                               std::numeric_limits<int32_t>::max()) ||
        json_reader::ReadField(body, "sdpMLineIndex",
                               &candidate.sdp_mline_index, 0,
                               std::numeric_limits<int32_t>::max());
    if (!has_mline_index) {
        return HttpMediaStatusResponse(400, "Missing candidate fields");
    }
    if (!json_reader::ReadField(body, "username_fragment",
                                &candidate.username_fragment)) {
        (void)json_reader::ReadField(body, "usernameFragment",
                                     &candidate.username_fragment);
    }

    Json root = Json::object();
    root["peer_id"] = candidate.peer_id;
    if (webrtc->AddIceCandidate(candidate)) {
        return HttpMediaJsonResponse(200, root);
    }
    return WebrtcErrorResponse(404, "peer_not_found",
                               "WebRTC peer not found");
}

HttpResponse BuildClosePeerResponse(IWebrtc *webrtc,
                                    const HttpRequest &request,
                                    const Json &body,
                                    const AuthPrincipal &principal) {
    (void)body;
    (void)principal;
    std::string peer_id;
    if (!ParsePeerPath(request, &peer_id)) {
        return HttpMediaStatusResponse(400, "Missing peer_id");
    }
    Json root = Json::object();
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
        return HttpMediaTextResponse(
            503, peer.last_error.empty() ? "Could not create WHEP peer"
                                         : peer.last_error);
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
    explicit WebrtcHttpHandler(
        const HttpMediaHandlerDependencies &dependencies)
        : access_(dependencies.access),
          device_(dependencies.device),
          webrtc_(dependencies.webrtc) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (webrtc_ != nullptr) {
            router.AddExactRoute(HttpMethod::kPost, "/api/webrtc/peers",
                                 this, &WebrtcHttpHandler::HandleCreatePeer);
            router.AddPrefixRoute(HttpMethod::kPost, "/api/webrtc/peers/",
                                  this, &WebrtcHttpHandler::HandlePeerPost);
            router.AddPrefixRoute(HttpMethod::kDelete, "/api/webrtc/peers/",
                                  this, &WebrtcHttpHandler::HandleClosePeer);
        }
        router.AddPrefixRoute(HttpMethod::kPost, "/live/",
                              this, &WebrtcHttpHandler::HandleWhepCreate);
        router.AddPrefixRoute(HttpMethod::kDelete, "/live/",
                              this, &WebrtcHttpHandler::HandleWhepDelete);
    }

private:
    HttpResponse HandleCreatePeer(const HttpRequest &request) {
        return HandleWebrtc(request, &BuildCreatePeerResponse);
    }

    HttpResponse HandlePeerPost(const HttpRequest &request) {
        if (ParsePeerSubPath(request, "/offer", nullptr)) {
            return HandleWebrtc(request, &BuildOfferResponse);
        }
        if (ParsePeerSubPath(request, "/candidates", nullptr)) {
            return HandleWebrtc(request, &BuildCandidateResponse);
        }
        return WebrtcErrorResponse(404, "invalid_argument", "Not Found");
    }

    HttpResponse HandleClosePeer(const HttpRequest &request) {
        return HandleWebrtc(request, &BuildClosePeerResponse);
    }

    HttpResponse HandleWebrtc(const HttpRequest &request,
                              WebrtcRouteHandler handler) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (device_ != nullptr && device_->IsRestarting()) {
            return WebrtcErrorResponse(503, "resource_busy",
                                       "Media pipeline restarting");
        }

        Json body;
        if (!ParseOptionalHttpMediaJsonBody(request, &body)) {
            return WebrtcErrorResponse(400, "invalid_argument",
                                       "Invalid JSON");
        }

        return handler(webrtc_, request, body, principal);
    }

    HttpResponse HandleWhepCreate(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireLiveStreamAuthResponse(access_, request,
                                          &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (webrtc_ == nullptr) {
            return HttpMediaTextResponse(501, "Not Implemented");
        }
        if (device_ != nullptr && device_->IsRestarting()) {
            return HttpMediaTextResponse(503, "Media pipeline restarting");
        }
        return BuildWhepCreateResponse(webrtc_, request, principal);
    }

    HttpResponse HandleWhepDelete(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireLiveStreamAuthResponse(access_, request,
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
    DeviceMedia *device_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeWebrtcHandler(
    const HttpMediaHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new WebrtcHttpHandler(dependencies));
}

}  // namespace live_stream
