#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "http_request_utils.h"
#include "infra/log.h"
#include "stream_hub_service.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace live_stream {
namespace {

const char *VideoCodecName(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::kH264:
            return "h264";
        case VideoCodec::kH265:
            return "h265";
        case VideoCodec::kMjpeg:
            return "mjpeg";
        case VideoCodec::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

bool RequestBrowserKeyFrame(IStreamHubService *stream_hub_service,
                            StreamId stream_id) {
    return stream_hub_service != nullptr &&
           stream_hub_service->RequestKeyFrame(stream_id,
                                               KeyFrameReason::kNewClient);
}

HttpResponse BuildPlaylistResponse(const StreamHlsPlaylist &playlist,
                                   const std::string &token) {
    const std::string suffix =
        token.empty() ? std::string() : std::string("?token=") + token;
    std::string body;
    body += "#EXTM3U\n";
    body += "#EXT-X-VERSION:3\n";
    body += "#EXT-X-TARGETDURATION:" +
            std::to_string(playlist.target_duration_sec) + "\n";
    body += "#EXT-X-MEDIA-SEQUENCE:" +
            std::to_string(playlist.media_sequence) + "\n";
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

bool ParseHlsPath(const HttpRequest &request, StreamId *stream_id,
                  std::string *object_name) {
    if (stream_id == nullptr || object_name == nullptr) {
        return false;
    }
    const std::string remaining = PathSuffix(request.path, "/api/hls/");
    const size_t slash = remaining.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash + 1 >= remaining.size()) {
        return false;
    }
    const std::string stream_name = remaining.substr(0, slash);
    if (!StreamIdFromJsonString(stream_name, stream_id)) {
        return false;
    }
    *object_name = remaining.substr(slash + 1);
    return true;
}

HttpResponse HandlePlaylist(IStreamHubService *stream_hub,
                            const HttpRequest &request, StreamId stream_id,
                            const std::string &object_name,
                            const StreamBrowserStatus &browser_status) {
    bool keyframe_requested = RequestBrowserKeyFrame(stream_hub, stream_id);
    StreamHlsPlaylist playlist = stream_hub->GetHlsPlaylist(stream_id);
    if (playlist.entries.empty()) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HLS reject stream=%s object=%s reason=empty "
                        "codec=%s running=%d hls_ready=%d keyframe=%d "
                        "segments=%u current_segment=%u",
                        StreamIdToJsonString(stream_id), object_name.c_str(),
                        VideoCodecName(browser_status.codec),
                        browser_status.running ? 1 : 0,
                        browser_status.hls_ready ? 1 : 0,
                        keyframe_requested ? 1 : 0,
                        browser_status.hls_segment_count,
                        browser_status.hls_current_segment_size);
        return StatusResponse(503, "HLS playlist not ready");
    }
    return BuildPlaylistResponse(playlist, ExtractBearerToken(request));
}

HttpResponse HandleSegment(IStreamHubService *stream_hub, StreamId stream_id,
                           const std::string &object_name) {
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

    const StreamSegment segment =
        stream_hub->GetHlsSegment(stream_id, static_cast<uint64_t>(sequence));
    if (!segment.found) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HLS reject stream=%s object=%s reason=segment_missing "
                        "sequence=%llu",
                        StreamIdToJsonString(stream_id), object_name.c_str(),
                        sequence);
        return StatusResponse(404, "HLS segment not found");
    }

    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "video/mp2t";
    response.body = segment.body;
    return response;
}

}  // namespace

class HlsHttpHandler : public IHttpHandler {
public:
    HlsHttpHandler(HttpHandlerContext *context,
                   const MediaHandlerDependencies &dependencies)
        : context_(context), dependencies_(dependencies) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddPrefixRoute(HttpMethod::kGet, "/api/hls/",
                               &HlsHttpHandler::HandleHlsRoute, this);
    }

private:
    static HttpResponse HandleHlsRoute(void *user,
                                       const HttpRequest &request) {
        return static_cast<HlsHttpHandler *>(user)->HandleHls(request);
    }

    HttpResponse HandleHls(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireAuth(context_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        if (dependencies_.stream_hub_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        if (IsMediaRestarting(dependencies_.media_service)) {
            return StatusResponse(503, "Media pipeline restarting");
        }

        StreamId stream_id = StreamId::kMain;
        std::string object_name;
        if (!ParseHlsPath(request, &stream_id, &object_name)) {
            return StatusResponse(400, "Invalid HLS path");
        }

        const StreamBrowserStatus browser_status =
            dependencies_.stream_hub_service->GetBrowserStatus(stream_id);
        if (!browser_status.browser_codec) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HLS reject stream=%s object=%s reason=unsupported "
                            "codec=%s running=%d hls_ready=%d segments=%u "
                            "current_segment=%u",
                            StreamIdToJsonString(stream_id),
                            object_name.c_str(),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.hls_ready ? 1 : 0,
                            browser_status.hls_segment_count,
                            browser_status.hls_current_segment_size);
            return StatusResponse(409, "HLS requires H.264 or H.265 stream");
        }
        if (!browser_status.running) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HLS reject stream=%s object=%s reason=not_ready "
                            "codec=%s running=%d hls_ready=%d "
                            "segments=%u current_segment=%u",
                            StreamIdToJsonString(stream_id),
                            object_name.c_str(),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.hls_ready ? 1 : 0,
                            browser_status.hls_segment_count,
                            browser_status.hls_current_segment_size);
            return StatusResponse(503, "HLS playlist not ready");
        }

        if (object_name == "index.m3u8") {
            return HandlePlaylist(dependencies_.stream_hub_service, request,
                                  stream_id, object_name, browser_status);
        }
        return HandleSegment(dependencies_.stream_hub_service, stream_id,
                             object_name);
    }

    HttpHandlerContext *context_ = nullptr;
    MediaHandlerDependencies dependencies_;
};

std::unique_ptr<IHttpHandler> CreateHlsHttpHandler(
    HttpHandlerContext *context,
    const MediaHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new HlsHttpHandler(context, dependencies));
}

}  // namespace live_stream
