#include "http_media.h"

#include "http_media_utils.h"
#include "http_router.h"

#include "device_media.h"
#include "infra/log.h"

#include <cstdio>
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

bool RequestBrowserKeyFrame(IMediaSource *media_source,
                            StreamId stream_id) {
    return media_source != nullptr &&
           media_source->RequestKeyFrame(stream_id,
                                               KeyFrameReason::kNewClient);
}

std::string SegmentQuerySuffix(const HttpRequest &request) {
    if (request.query_string.empty()) {
        return std::string();
    }
    return "?" + request.query_string;
}

HttpResponse BuildPlaylistResponse(const MediaHlsPlaylist &playlist,
                                   const HttpRequest &request) {
    std::string body;
    const std::string segment_query = SegmentQuerySuffix(request);
    body += "#EXTM3U\n";
    body += "#EXT-X-VERSION:3\n";
    body += "#EXT-X-TARGETDURATION:" +
            std::to_string(playlist.target_duration_sec == 0
                               ? 1
                               : playlist.target_duration_sec) +
            "\n";
    body += "#EXT-X-MEDIA-SEQUENCE:" +
            std::to_string(playlist.media_sequence) + "\n";
    body += "#EXT-X-INDEPENDENT-SEGMENTS\n";
    for (const MediaHlsEntry &entry : playlist.entries) {
        const double duration =
            static_cast<double>(entry.duration_us) / 1000000.0;
        char line[64];
        std::snprintf(line, sizeof(line), "#EXTINF:%.3f,\n", duration);
        body += line;
        body += "seg-" + std::to_string(entry.sequence) + ".ts" +
                segment_query + "\n";
    }

    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "application/vnd.apple.mpegurl";
    response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate";
    response.headers["Pragma"] = "no-cache";
    response.headers["Expires"] = "0";
    response.body = body;
    return response;
}

bool ParseHlsPath(const HttpRequest &request, StreamId *stream_id,
                  std::string *object_name) {
    if (stream_id == nullptr || object_name == nullptr) {
        return false;
    }
    const std::string remaining = HttpMediaPathSuffix(request.path, "/live/");
    const size_t slash = remaining.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash + 1 >= remaining.size()) {
        return false;
    }
    const std::string stream_name = remaining.substr(0, slash);
    if (!MediaStreamIdFromJson(stream_name, stream_id)) {
        return false;
    }
    const std::string hls_path = remaining.substr(slash + 1);
    if (!HttpMediaStartsWith(hls_path, "hls/") || hls_path.size() <= 4) {
        return false;
    }
    *object_name = hls_path.substr(4);
    return true;
}

HttpResponse HandlePlaylist(IMediaSource *media_source,
                            const HttpRequest &request,
                            StreamId stream_id, const std::string &object_name,
                            const MediaSourceStatus &browser_status) {
    // playlist 请求是浏览器开始预览的信号，顺手请求关键帧可以缩短首个完整
    // HLS segment 生成时间；真正的 segment 数据仍由 media_source 缓存提供。
    bool keyframe_requested = RequestBrowserKeyFrame(media_source, stream_id);
    MediaHlsPlaylist playlist = media_source->GetHlsPlaylist(stream_id);
    if (playlist.entries.empty()) {
        Debug(kHttpMediaModuleName,
                        "HLS warmup stream=%s object=%s codec=%s "
                        "keyframe=%d segments=%u range=%llu-%llu "
                        "missing=%llu evicted=%llu current_segment=%u",
                        MediaStreamIdToJson(stream_id), object_name.c_str(),
                        VideoCodecName(browser_status.codec),
                        keyframe_requested ? 1 : 0,
                        browser_status.hls_segment_count,
                        static_cast<unsigned long long>(
                            browser_status.hls_first_segment_sequence),
                        static_cast<unsigned long long>(
                            browser_status.hls_last_segment_sequence),
                        static_cast<unsigned long long>(
                            browser_status.hls_missing_segment_count),
                        static_cast<unsigned long long>(
                            browser_status.hls_evicted_segment_count),
                        browser_status.hls_current_segment_size);
        return BuildPlaylistResponse(playlist, request);
    }
    return BuildPlaylistResponse(playlist, request);
}

}  // namespace

class HlsHttpHandler : public IHttpHandler {
public:
    HlsHttpHandler(HttpAccess *access,
                   IDeviceMedia *device_media,
                   IMediaSource *media_source)
        : access_(access), device_media_(device_media),
          media_source_(media_source) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddPrefixRoute(HttpMethod::kGet, "/live/",
                               &HlsHttpHandler::HandleHlsRoute, this);
    }

private:
    static HttpResponse HandleHlsRoute(void *user,
                                       const HttpRequest &request) {
        return static_cast<HlsHttpHandler *>(user)->HandleHls(request);
    }

    HttpResponse HandleHls(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequirePlaybackAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (media_source_ == nullptr) {
            return HttpMediaTextResponse(501, "Not Implemented");
        }
        if (IsHttpMediaRestarting(device_media_)) {
            return HttpMediaTextResponse(503, "Media pipeline restarting");
        }

        StreamId stream_id = StreamId::kMain;
        std::string object_name;
        if (!ParseHlsPath(request, &stream_id, &object_name)) {
            return HttpMediaTextResponse(404, "Not Found");
        }

        const MediaSourceStatus browser_status =
            media_source_->GetBrowserStatus(stream_id);
        if (!browser_status.hls_supported) {
            Error(kHttpMediaModuleName,
                            "HLS reject stream=%s object=%s reason=unsupported "
                            "codec=%s running=%d hls_ready=%d segments=%u "
                            "range=%llu-%llu missing=%llu evicted=%llu "
                            "current_segment=%u",
                            MediaStreamIdToJson(stream_id),
                            object_name.c_str(),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.hls_ready ? 1 : 0,
                            browser_status.hls_segment_count,
                            static_cast<unsigned long long>(
                                browser_status.hls_first_segment_sequence),
                            static_cast<unsigned long long>(
                                browser_status.hls_last_segment_sequence),
                            static_cast<unsigned long long>(
                                browser_status.hls_missing_segment_count),
                            static_cast<unsigned long long>(
                                browser_status.hls_evicted_segment_count),
                            browser_status.hls_current_segment_size);
            return HttpMediaTextResponse(
                409, "HLS requires H.264 or H.265 stream");
        }
        if (!browser_status.running) {
            Error(kHttpMediaModuleName,
                            "HLS reject stream=%s object=%s reason=not_ready "
                            "codec=%s running=%d hls_ready=%d "
                            "segments=%u range=%llu-%llu missing=%llu "
                            "evicted=%llu current_segment=%u",
                            MediaStreamIdToJson(stream_id),
                            object_name.c_str(),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.hls_ready ? 1 : 0,
                            browser_status.hls_segment_count,
                            static_cast<unsigned long long>(
                                browser_status.hls_first_segment_sequence),
                            static_cast<unsigned long long>(
                                browser_status.hls_last_segment_sequence),
                            static_cast<unsigned long long>(
                                browser_status.hls_missing_segment_count),
                            static_cast<unsigned long long>(
                                browser_status.hls_evicted_segment_count),
                            browser_status.hls_current_segment_size);
            return HttpMediaTextResponse(503, "HLS playlist not ready");
        }

        if (object_name == "index.m3u8") {
            return HandlePlaylist(media_source_, request, stream_id,
                                  object_name, browser_status);
        }
        return HttpMediaTextResponse(404, "Not Found");
    }

    HttpAccess *access_ = nullptr;
    IDeviceMedia *device_media_ = nullptr;
    IMediaSource *media_source_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeHlsHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    IMediaSource *media_source) {
    return std::unique_ptr<IHttpHandler>(
        new HlsHttpHandler(access, device_media, media_source));
}

}  // namespace live_stream
