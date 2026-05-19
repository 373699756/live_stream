// handlers/snapshot_handler.cpp.inc
// Snapshot handlers: /api/snapshot/*

HttpResponse HandleSnapshot(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.snapshot_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
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
