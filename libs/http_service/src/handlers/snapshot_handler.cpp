#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "media/stream_types.h"
#include "media_service.h"
#include "snapshot_service.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {

HttpResponse http_handlers::HandleSnapshot(HttpHandlerContext *context, const HttpRequest &request) {
    AuthPrincipal principal;
    if (!RequireAuth(context, request, &principal)) {
        return StatusResponse(401, "Unauthorized");
    }
    if (context->Dependencies().snapshot_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    if (context->Dependencies().media_service != nullptr &&
        context->Dependencies().media_service->IsRestarting()) {
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
        context->Dependencies().snapshot_service->Capture(capture_request);
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr) {
        return StatusResponse(500, "Invalid snapshot frame");
    }
    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "image/jpeg";
    response.body.assign(reinterpret_cast<const char *>(data), frame.size);
    return response;
}

}  // namespace live_stream
