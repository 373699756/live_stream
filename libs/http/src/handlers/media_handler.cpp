#include "handlers/http_handlers.h"

#include "handlers/media_preview_response.h"
#include "handlers/media_session_response.h"
#include "handlers/media_stream_response.h"
#include "http_auth_gate.h"
#include "http_path.h"
#include "http_response.h"
#include "http_stream_id_json.h"

#include "device.h"
#include "http.h"
#include "media/media_streams.h"
#include "rtsp.h"
#include "webrtc.h"

#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class MediaHttpHandler : public IHttpHandler {
public:
    explicit MediaHttpHandler(const MediaHandlerRefs &refs)
        : access_(refs.access),
          config_(refs.config),
          device_(refs.device),
          media_streams_(refs.media_streams),
          rtsp_session_reader_(refs.rtsp_session_reader),
          webrtc_reader_(refs.webrtc_reader),
          net_stat_(refs.net_stat),
          http_(refs.http) {}

    void RegisterRoutes(IHttpRouter &router) override {
        router.AddExactRoute(HttpMethod::kGet, "/api/media/streams",
                             this, &MediaHttpHandler::HandleStreams);
        if (device_ != nullptr) {
            router.AddExactRoute(HttpMethod::kGet, "/api/media/capabilities",
                                 this, &MediaHttpHandler::HandleCapabilities);
        }
        router.AddPrefixRoute(HttpMethod::kGet, "/api/media/streams/",
                              this, &MediaHttpHandler::HandleStream);
        router.AddExactRoute(HttpMethod::kGet, "/api/media/sessions",
                             this, &MediaHttpHandler::HandleSessions);
    }

private:
    HttpResponse HandleStreams(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "media",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json root = Json::object();
        Json items = Json::array();
        items.push_back(BuildMediaStreamResponse(StreamId::kMain, config_,
                                                 device_, media_streams_,
                                                 webrtc_reader_));
        items.push_back(BuildMediaStreamResponse(StreamId::kSub, config_,
                                                 device_, media_streams_,
                                                 webrtc_reader_));
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "media",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return JsonResponse(
            200, BuildMediaCapabilitiesResponse(device_->GetCapabilities()));
    }

    HttpResponse HandleStream(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "media",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
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
                200, BuildMediaPreviewResponse(config_, rtsp_session_reader_,
                                               request, stream_id));
        }
        return JsonResponse(
            200, BuildMediaStreamResponse(stream_id, config_, device_,
                                          media_streams_, webrtc_reader_));
    }

    HttpResponse HandleSessions(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "media",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json root = Json::object();
        Json items = Json::array();
        // 统一会话视图：协议模块负责自己的业务会话，socket_io/NetStat 只补网络发送诊断。
        // HTTP handler 不反向控制 RTSP/WebRTC/NetStat，避免诊断接口变成跨模块控制面。
        if (rtsp_session_reader_ != nullptr) {
            const std::vector<RtspSessionInfo> sessions =
                rtsp_session_reader_->ListSessionInfo();
            for (const RtspSessionInfo &session : sessions) {
                items.push_back(BuildRtspSessionResponse(session));
            }
        }
        if (http_ != nullptr) {
            const std::vector<HttpStreamSessionInfo> sessions =
                http_->ListStreamSessionInfo();
            for (const HttpStreamSessionInfo &session : sessions) {
                if (IsMediaStreamingSession(session)) {
                    items.push_back(BuildHttpStreamingSessionResponse(
                        session, media_streams_));
                }
            }
        }
        WebrtcStats webrtc_stats;
        if (webrtc_reader_ != nullptr) {
            webrtc_stats = webrtc_reader_->GetStats();
            const std::vector<WebrtcPeerInfo> peers =
                webrtc_reader_->GetPeers();
            for (const WebrtcPeerInfo &peer : peers) {
                items.push_back(BuildWebrtcSessionResponse(peer));
            }
        }
        AddWebrtcStatsToResponse(&root, webrtc_stats);
        AddNetStatToResponse(&root, net_stat_);
        MediaStreamStats media_stats;
        if (media_streams_ != nullptr) {
            media_stats = media_streams_->GetStreamStats();
        }
        root["http_flv_active_clients"] = media_stats.active_flv_clients;
        root["mjpeg_active_clients"] = media_stats.active_mjpeg_clients;
        root["rtsp_active_sessions"] =
            rtsp_session_reader_ == nullptr
                ? 0
                : rtsp_session_reader_->GetStats().active_sessions;
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpAccess *access_ = nullptr;
    IConfig *config_ = nullptr;
    DeviceMedia *device_ = nullptr;
    MediaStreams *media_streams_ = nullptr;
    IRtspSessionReader *rtsp_session_reader_ = nullptr;
    IWebrtcReader *webrtc_reader_ = nullptr;
    INetStat *net_stat_ = nullptr;
    IHttp *http_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeMediaHandler(
    const MediaHandlerRefs &refs) {
    return std::unique_ptr<IHttpHandler>(
        new MediaHttpHandler(refs));
}

}  // namespace live_stream
