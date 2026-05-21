#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "media/stream_types.h"
#include "media_service.h"
#include "snapshot_service.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {

namespace {

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.size >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

}  // namespace

class SnapshotHttpHandler : public IHttpHandler {
public:
    SnapshotHttpHandler(HttpHandlerContext *context,
                        const MediaHandlerDependencies &dependencies)
        : context_(context), dependencies_(dependencies) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddPrefixRoute(HttpMethod::kGet, "/api/snapshot/",
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
        if (!RequireAuth(context_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        if (dependencies_.snapshot_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        if (IsMediaRestarting(dependencies_.media_service)) {
            return StatusResponse(503, "Media pipeline restarting");
        }
        const std::string prefix = "/api/snapshot/";
        std::string name = request.path.substr(prefix.size());
        const size_t dot = name.find('.');
        if (dot != std::string::npos) {
            name = name.substr(0, dot);
        }
        StreamId stream_id = StreamId::kMain;
        if (!StreamIdFromJsonString(name, &stream_id)) {
            return StatusResponse(400, "Invalid stream");
        }
        CaptureRequest capture_request;
        capture_request.stream_id = stream_id;
        SnapshotFrame frame =
            dependencies_.snapshot_service->Capture(capture_request);
        if (!frame.HasValidPayload() || !LooksLikeJpeg(frame)) {
            return StatusResponse(500, "Invalid snapshot frame");
        }
        const uint8_t *data = frame.PayloadData();
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "image/jpeg";
        response.headers["Cache-Control"] = "no-cache";
        response.body.assign(reinterpret_cast<const char *>(data), frame.size);
        return response;
    }

    HttpHandlerContext *context_ = nullptr;
    MediaHandlerDependencies dependencies_;
};

std::unique_ptr<IHttpHandler> CreateSnapshotHttpHandler(
    HttpHandlerContext *context,
    const MediaHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new SnapshotHttpHandler(context, dependencies));
}

}  // namespace live_stream
