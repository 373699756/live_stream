#include "http_media.h"

#include "http_media_auth.h"
#include "http_media_module.h"
#include "http_media_path.h"
#include "http_media_response.h"
#include "http_media_stream_id_json.h"
#include "http_router.h"

#include "device.h"
#include "infra/log.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace live_stream {
namespace {

const char *CodecName(Codec codec) {
    switch (codec) {
        case Codec::kH264:
            return "h264";
        case Codec::kH265:
            return "h265";
        case Codec::kMjpeg:
            return "mjpeg";
        case Codec::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

bool RequestPreviewKeyframe(MediaStreams &media_streams,
                            StreamId stream_id) {
    return media_streams.RequestKeyframe(stream_id,
                                         KeyframeRequestSource::kNewClient);
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

bool ParseHlsPath(const HttpRequest &request, StreamId &stream_id,
                  std::string &object_name) {
    const std::string remaining = HttpMediaPathSuffix(request.path, "/live/");
    const size_t slash = remaining.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash + 1 >= remaining.size()) {
        return false;
    }
    const std::string stream_name = remaining.substr(0, slash);
    if (!MediaStreamIdFromJson(stream_name, &stream_id)) {
        return false;
    }
    const std::string hls_path = remaining.substr(slash + 1);
    if (!HttpMediaStartsWith(hls_path, "hls/") || hls_path.size() <= 4) {
        return false;
    }
    object_name = hls_path.substr(4);
    return true;
}

bool IsHlsSegmentObjectName(const std::string &object_name) {
    return HttpMediaStartsWith(object_name, "seg-") && object_name.size() > 7 &&
           object_name.substr(object_name.size() - 3) == ".ts";
}

bool ParseHlsSegmentSequence(const std::string &object_name,
                             uint64_t &sequence) {
    if (!IsHlsSegmentObjectName(object_name)) {
        return false;
    }
    const std::string sequence_text =
        object_name.substr(4, object_name.size() - 7);
    char *end = nullptr;
    const unsigned long long parsed =
        std::strtoull(sequence_text.c_str(), &end, 10);
    if (*end != '\0') {
        return false;
    }
    sequence = static_cast<uint64_t>(parsed);
    return true;
}

HttpResponse HandlePlaylist(MediaStreams &media_streams,
                            const HttpRequest &request,
                            StreamId stream_id, const std::string &object_name,
                            const MediaStreamInfo &stream_info) {
    // playlist 请求是浏览器开始预览的信号，顺手请求关键帧可以缩短首个完整
    // HLS segment 生成时间；真正的 segment 数据仍由 MediaStreams 缓存提供。
    bool keyframe_requested = RequestPreviewKeyframe(media_streams, stream_id);
    MediaHlsPlaylist playlist = media_streams.GetHlsPlaylist(stream_id);
    if (playlist.entries.empty()) {
        Debug(kHttpMediaModuleName,
              "HLS warmup stream=%s object=%s codec=%s "
              "keyframe=%d segments=%u range=%llu-%llu "
              "missing=%llu evicted=%llu current_segment=%u",
              MediaStreamIdToJson(stream_id), object_name.c_str(),
              CodecName(stream_info.codec),
              keyframe_requested ? 1 : 0,
              stream_info.hls_segment_size,
              static_cast<unsigned long long>(
                  stream_info.hls_first_segment_sequence),
              static_cast<unsigned long long>(
                  stream_info.hls_last_segment_sequence),
              static_cast<unsigned long long>(
                  stream_info.hls_missing_segments),
              static_cast<unsigned long long>(
                  stream_info.hls_evicted_segments),
              stream_info.hls_current_segment_size);
        return BuildPlaylistResponse(playlist, request);
    }
    return BuildPlaylistResponse(playlist, request);
}

HttpResponse HandleSegment(MediaStreams &media_streams, StreamId stream_id,
                           const std::string &object_name,
                           const MediaStreamInfo &stream_info) {
    uint64_t sequence = 0;
    if (!ParseHlsSegmentSequence(object_name, sequence)) {
        return HttpMediaTextResponse(400, "Invalid HLS segment");
    }

    MediaSegmentRef segment =
        media_streams.GetHlsSegmentRef(stream_id, sequence);
    if (!segment.found || !segment.body.Valid() ||
        segment.body.Size() == 0) {
        Error(kHttpMediaModuleName,
              "HLS reject stream=%s object=%s "
              "reason=segment_missing sequence=%llu range=%llu-%llu "
              "segments=%u missing=%llu evicted=%llu",
              MediaStreamIdToJson(stream_id), object_name.c_str(),
              static_cast<unsigned long long>(sequence),
              static_cast<unsigned long long>(
                  stream_info.hls_first_segment_sequence),
              static_cast<unsigned long long>(
                  stream_info.hls_last_segment_sequence),
              stream_info.hls_segment_size,
              static_cast<unsigned long long>(
                  stream_info.hls_missing_segments),
              static_cast<unsigned long long>(
                  stream_info.hls_evicted_segments));
        return HttpMediaTextResponse(404, "HLS segment not found");
    }

    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "video/mp2t";
    response.body_slices.emplace_back(segment.body.Data(), segment.body.Size(),
                                      segment.body);
    return response;
}

}  // namespace

class HlsHttpHandler : public IHttpHandler {
public:
    explicit HlsHttpHandler(
        const HttpMediaHandlerDependencies &dependencies)
        : access_(dependencies.access),
          device_(dependencies.device),
          media_streams_(dependencies.media_streams) {}

    void RegisterRoutes(IHttpRouter &router) override {
        router.AddPrefixRoute(HttpMethod::kGet, "/live/",
                              this, &HlsHttpHandler::HandleHls);
    }

private:
    HttpResponse HandleHls(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireLiveStreamAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (media_streams_ == nullptr) {
            return HttpMediaTextResponse(501, "Not Implemented");
        }
        if (device_ != nullptr && device_->IsRestarting()) {
            return HttpMediaTextResponse(503, "Media pipeline restarting");
        }

        StreamId stream_id = StreamId::kMain;
        std::string object_name;
        if (!ParseHlsPath(request, stream_id, object_name)) {
            return HttpMediaTextResponse(404, "Not Found");
        }

        const MediaStreamInfo stream_info =
            media_streams_->GetStreamInfo(stream_id);
        if (!stream_info.hls_supported) {
            Error(kHttpMediaModuleName,
                  "HLS reject stream=%s object=%s reason=unsupported "
                  "codec=%s running=%d hls_ready=%d segments=%u "
                  "range=%llu-%llu missing=%llu evicted=%llu "
                  "current_segment=%u",
                  MediaStreamIdToJson(stream_id),
                  object_name.c_str(),
                  CodecName(stream_info.codec),
                  stream_info.running ? 1 : 0,
                  stream_info.hls_ready ? 1 : 0,
                  stream_info.hls_segment_size,
                  static_cast<unsigned long long>(
                      stream_info.hls_first_segment_sequence),
                  static_cast<unsigned long long>(
                      stream_info.hls_last_segment_sequence),
                  static_cast<unsigned long long>(
                      stream_info.hls_missing_segments),
                  static_cast<unsigned long long>(
                      stream_info.hls_evicted_segments),
                  stream_info.hls_current_segment_size);
            return HttpMediaTextResponse(
                409, "HLS requires H.264 or H.265 stream");
        }
        if (!stream_info.running) {
            Error(kHttpMediaModuleName,
                  "HLS reject stream=%s object=%s reason=not_ready "
                  "codec=%s running=%d hls_ready=%d "
                  "segments=%u range=%llu-%llu missing=%llu "
                  "evicted=%llu current_segment=%u",
                  MediaStreamIdToJson(stream_id),
                  object_name.c_str(),
                  CodecName(stream_info.codec),
                  stream_info.running ? 1 : 0,
                  stream_info.hls_ready ? 1 : 0,
                  stream_info.hls_segment_size,
                  static_cast<unsigned long long>(
                      stream_info.hls_first_segment_sequence),
                  static_cast<unsigned long long>(
                      stream_info.hls_last_segment_sequence),
                  static_cast<unsigned long long>(
                      stream_info.hls_missing_segments),
                  static_cast<unsigned long long>(
                      stream_info.hls_evicted_segments),
                  stream_info.hls_current_segment_size);
            return HttpMediaTextResponse(503, "HLS playlist not ready");
        }

        if (object_name == "index.m3u8") {
            return HandlePlaylist(*media_streams_, request, stream_id,
                                  object_name, stream_info);
        }
        if (IsHlsSegmentObjectName(object_name)) {
            return HandleSegment(*media_streams_, stream_id, object_name,
                                 stream_info);
        }
        return HttpMediaTextResponse(404, "Not Found");
    }

    HttpAccess *access_ = nullptr;
    DeviceMedia *device_ = nullptr;
    MediaStreams *media_streams_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeHlsHandler(
    const HttpMediaHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new HlsHttpHandler(dependencies));
}

}  // namespace live_stream
