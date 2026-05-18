// handlers/stream_handler.cpp.inc
// Streaming handlers: /api/hls/*, /api/webrtc/*, /api/flv/*

HttpResponse HandleHls(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.stream_hub_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }

    const std::string prefix = "/api/hls/";
    const std::string remaining = request.path.substr(prefix.size());
    const size_t slash = remaining.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash + 1 >= remaining.size()) {
        return StatusResponse(400, "Invalid HLS path");
    }
    const std::string stream_name = remaining.substr(0, slash);
    const std::string object_name = remaining.substr(slash + 1);
    StreamId stream_id = StreamId::kMain;
    if (!StreamIdFromJsonString(stream_name, &stream_id)) {
        return StatusResponse(400, "Invalid stream");
    }
    if (!dependencies_.stream_hub_service->IsHlsSupported(stream_id)) {
        return StatusResponse(409, "HLS requires H.264 or H.265 stream");
    }

    if (object_name == "index.m3u8") {
        const StreamHlsPlaylist playlist =
            dependencies_.stream_hub_service->GetHlsPlaylist(stream_id);
        if (playlist.entries.empty()) {
            return StatusResponse(503, "HLS playlist not ready");
        }
        const std::string token = ExtractBearerToken(request);
        const std::string suffix =
            token.empty() ? std::string() : std::string("?token=") + token;
        std::string body;
        body += "#EXTM3U\n";
        body += "#EXT-X-VERSION:3\n";
        body += "#EXT-X-TARGETDURATION:" +
                std::to_string(playlist.target_duration_sec) + "\n";
        body +=
            "#EXT-X-MEDIA-SEQUENCE:" + std::to_string(playlist.media_sequence) +
            "\n";
        body += "#EXT-X-INDEPENDENT-SEGMENTS\n";
        for (const StreamHlsEntry &entry : playlist.entries) {
            const double duration =
                static_cast<double>(entry.duration_us) / 1000000.0;
            char line[64];
            std::snprintf(line, sizeof(line), "#EXTINF:%.3f,\n", duration);
            body += line;
            body += "seg-" + std::to_string(entry.sequence) + ".ts" + suffix + "\n";
        }
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "application/vnd.apple.mpegurl";
        response.body = body;
        return response;
    }

    if (!StartsWith(object_name, "seg-") || object_name.size() <= 7 ||
        object_name.substr(object_name.size() - 3) != ".ts") {
        return StatusResponse(404, "Not Found");
    }
    const std::string sequence_text =
        object_name.substr(4, object_name.size() - 7);
    char *end = nullptr;
    const unsigned long long sequence =
        std::strtoull(sequence_text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return StatusResponse(400, "Invalid HLS segment");
    }
    const StreamSegment segment = dependencies_.stream_hub_service->GetHlsSegment(
        stream_id, static_cast<uint64_t>(sequence));
    if (!segment.found) {
        return StatusResponse(404, "HLS segment not found");
    }
    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "video/mp2t";
    response.body = segment.body;
    return response;
}

HttpResponse HandleWebrtc(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.webrtc_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    ConfigJson body = request.body.empty()
                          ? ConfigJson::object()
                          : ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return StatusResponse(400, "Invalid JSON");
    }

    if (request.path == "/api/webrtc/peers" &&
        request.method == HttpMethod::kPost) {
        WebrtcCreatePeerRequest create_request;
        std::string stream;
        StreamId stream_id = StreamId::kMain;
        if (!json_utils::Load(body, "stream", &stream) ||
            !StreamIdFromJsonString(stream, &stream_id) ||
            !json_utils::Load(body, "client_id", &create_request.client_id)) {
            return StatusResponse(400, "Invalid stream");
        }
        create_request.stream_id = stream_id;
        auto peer = dependencies_.webrtc_service->CreatePeer(create_request);
        if (peer.peer_id.empty()) {
            return StatusResponse(409, "Could not create peer");
        }
        ConfigJson root = ConfigJson::object();
        root["peer_id"] = peer.peer_id;
        root["stream"] = StreamIdToJsonString(peer.stream_id);
        return JsonResponse(200, root);
    }
    if (request.path == "/api/webrtc/offer" &&
        request.method == HttpMethod::kPost) {
        WebrtcOfferRequest offer;
        if (!json_utils::Load(body, "peer_id", &offer.peer_id) ||
            !json_utils::Load(body, "sdp", &offer.sdp)) {
            return StatusResponse(400, "Missing offer fields");
        }
        auto answer = dependencies_.webrtc_service->HandleOffer(offer);
        if (answer.sdp.empty()) {
            return StatusResponse(404, "Peer not found");
        }
        ConfigJson root = ConfigJson::object();
        root["peer_id"] = answer.peer_id;
        root["sdp"] = answer.sdp;
        return JsonResponse(200, root);
    }
    if (request.path == "/api/webrtc/candidate" &&
        request.method == HttpMethod::kPost) {
        WebrtcIceCandidate candidate;
        if (!json_utils::Load(body, "peer_id", &candidate.peer_id) ||
            !json_utils::Load(body, "candidate", &candidate.candidate) ||
            !json_utils::Load(body, "sdp_mid", &candidate.sdp_mid) ||
            !json_utils::Load(body, "sdp_mline_index", &candidate.sdp_mline_index,
                              0, std::numeric_limits<int32_t>::max())) {
            return StatusResponse(400, "Missing candidate fields");
        }
        return dependencies_.webrtc_service->AddIceCandidate(candidate)
                   ? OkResponse()
                   : StatusResponse(404, "Peer not found");
    }
    if (request.path == "/api/webrtc/close" &&
        request.method == HttpMethod::kPost) {
        std::string peer_id;
        if (!json_utils::Load(body, "peer_id", &peer_id)) {
            return StatusResponse(400, "Missing peer_id");
        }
        return dependencies_.webrtc_service->ClosePeer(peer_id)
                   ? OkResponse()
                   : StatusResponse(404, "Peer not found");
    }
    return StatusResponse(404, "Not Found");
}

void StartFlvStream(ConnectionId connection_id, const HttpRequest &request) {
    if (dependencies_.stream_hub_service == nullptr) {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV reject conn=%llu reason=no_stream_hub",
                        static_cast<unsigned long long>(connection_id));
        SendResponse(connection_id, StatusResponse(501, "Not Implemented"), true);
        return;
    }
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
        INFRA_LOG_ERROR(kModuleName, "HTTP-FLV reject conn=%llu reason=auth",
                        static_cast<unsigned long long>(connection_id));
        SendResponse(connection_id, StatusResponse(401, "Unauthorized"), true);
        return;
    }

    const std::string prefix = "/api/flv/";
    std::string stream_name = request.path.substr(prefix.size());
    if (stream_name.size() <= 4 ||
        stream_name.substr(stream_name.size() - 4) != ".flv") {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV reject conn=%llu reason=path path=%s",
                        static_cast<unsigned long long>(connection_id),
                        request.path.c_str());
        SendResponse(connection_id, StatusResponse(400, "Invalid FLV path"),
                     true);
        return;
    }
    stream_name.resize(stream_name.size() - 4);
    StreamId stream_id = StreamId::kMain;
    if (!StreamIdFromJsonString(stream_name, &stream_id)) {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV reject conn=%llu reason=stream stream=%s",
                        static_cast<unsigned long long>(connection_id),
                        stream_name.c_str());
        SendResponse(connection_id, StatusResponse(400, "Invalid stream"), true);
        return;
    }
    if (!dependencies_.stream_hub_service->IsFlvSupported(stream_id)) {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV reject conn=%llu stream=%s reason=unsupported",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        SendResponse(connection_id,
                     StatusResponse(409,
                                    "HTTP-FLV requires H.264 or H.265 stream"),
                     true);
        return;
    }

    const StreamFlvBootstrap bootstrap =
        dependencies_.stream_hub_service->GetFlvBootstrap(stream_id);
    if (!bootstrap.supported) {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV reject conn=%llu stream=%s reason=not_ready",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        SendResponse(connection_id, StatusResponse(503, "FLV stream not ready"),
                     true);
        return;
    }
    std::shared_ptr<IStreamFlvSink> sink(
        new FlvConnectionSink(this, connection_id));
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end()) {
            INFRA_LOG_ERROR(kModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=no_session",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            return;
        }
        iter->second.processing = false;
        iter->second.closing = true;
        iter->second.streaming = true;
        iter->second.flv_client_id = 0;
        iter->second.flv_sink = sink;
        iter->second.pending_requests.clear();
        iter->second.recv_buffer.clear();
        ++iter->second.timeout_generation;
    }

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "video/x-flv";
    headers["Cache-Control"] = "no-cache";
    headers["Pragma"] = "no-cache";
    const std::string header_block = BuildStreamingHeaderBlock(200, headers);
    INFRA_LOG_INFO(kModuleName,
                   "HTTP-FLV start conn=%llu stream=%s client=%llu header=%zu "
                   "file=%zu sequence=%zu keyframe=%zu",
                   static_cast<unsigned long long>(connection_id),
                   StreamIdToJsonString(stream_id),
                   static_cast<unsigned long long>(0),
                   header_block.size(), bootstrap.file_header.size(),
                   bootstrap.sequence_header.size(),
                   bootstrap.last_keyframe.size());
    if (!EnqueueStreamingChunk(
            connection_id,
            reinterpret_cast<const uint8_t *>(header_block.data()),
            header_block.size()) ||
        !EnqueueStreamingChunk(
            connection_id,
            reinterpret_cast<const uint8_t *>(bootstrap.file_header.data()),
            bootstrap.file_header.size()) ||
        (!bootstrap.sequence_header.empty() &&
         !EnqueueStreamingChunk(connection_id,
                                reinterpret_cast<const uint8_t *>(
                                    bootstrap.sequence_header.data()),
                                bootstrap.sequence_header.size())) ||
        (!bootstrap.last_keyframe.empty() &&
         !EnqueueStreamingChunk(
             connection_id,
             reinterpret_cast<const uint8_t *>(bootstrap.last_keyframe.data()),
             bootstrap.last_keyframe.size()))) {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV close conn=%llu stream=%s reason=enqueue",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        if (dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->Close(connection_id);
        }
        return;
    }

    const StreamFlvClientId client_id =
        dependencies_.stream_hub_service->AttachFlvClient(
            stream_id, bootstrap.config_generation, sink);
    if (client_id == 0) {
        INFRA_LOG_ERROR(kModuleName,
                        "HTTP-FLV close conn=%llu stream=%s reason=attach",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        if (dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->Close(connection_id);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || !iter->second.streaming) {
            (void)dependencies_.stream_hub_service->DetachFlvClient(client_id);
            INFRA_LOG_ERROR(kModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            return;
        }
        iter->second.flv_client_id = client_id;
    }
    INFRA_LOG_INFO(kModuleName, "HTTP-FLV attached conn=%llu stream=%s client=%llu",
                   static_cast<unsigned long long>(connection_id),
                   StreamIdToJsonString(stream_id),
                   static_cast<unsigned long long>(client_id));
}
