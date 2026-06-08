#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "media/stream_types.h"
#include "device_media.h"
#include "snapshot.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {

namespace {

HttpResponse SnapshotTextResponse(int status_code, const std::string &reason) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "text/plain";
    response.body = reason;
    return response;
}

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.size >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

}  // namespace

class SnapshotHttpHandler : public IHttpHandler {
public:
    SnapshotHttpHandler(HttpAccess *access,
                        IDeviceMedia *device_media,
                        ISnapshotView *snapshot)
        : access_(access), device_media_(device_media),
          snapshot_(snapshot) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddPrefixRoute(HttpMethod::kGet, "/snapshot/",
                               &SnapshotHttpHandler::HandleSnapshotRoute,
                               this);
    }

private:
    static HttpResponse HandleSnapshotRoute(void *user,
                                            const HttpRequest &request) {
        return static_cast<SnapshotHttpHandler *>(user)->HandleSnapshot(
            request);
    }

    HttpResponse HandleSnapshot(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (snapshot_ == nullptr) {
            return SnapshotTextResponse(501, "Not Implemented");
        }
        if (IsMediaRestarting(device_media_)) {
            return SnapshotTextResponse(503, "Media pipeline restarting");
        }
        const std::string prefix = "/snapshot/";
        std::string name = request.path.substr(prefix.size());
        const size_t dot = name.find('.');
        if (dot != std::string::npos) {
            name = name.substr(0, dot);
        }
        StreamId stream_id = StreamId::kMain;
        if (!StreamIdFromJsonString(name, &stream_id)) {
            return SnapshotTextResponse(400, "Invalid stream");
        }
        CaptureRequest capture_request;
        capture_request.stream_id = stream_id;
        SnapshotFrame frame =
            snapshot_->Capture(capture_request);
        if (!frame.HasValidPayload() || !LooksLikeJpeg(frame)) {
            return SnapshotTextResponse(500, "Invalid snapshot frame");
        }
        const uint8_t *data = frame.PayloadData();
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "image/jpeg";
        response.headers["Cache-Control"] = "no-cache";
        response.body.assign(reinterpret_cast<const char *>(data), frame.size);
        return response;
    }

    HttpAccess *access_ = nullptr;
    IDeviceMedia *device_media_ = nullptr;
    ISnapshotView *snapshot_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeSnapshotHandler(HttpAccess *access,
                                               IDeviceMedia *device_media,
                                               ISnapshotView *snapshot) {
    return std::unique_ptr<IHttpHandler>(
        new SnapshotHttpHandler(access, device_media, snapshot));
}

}  // namespace live_stream
