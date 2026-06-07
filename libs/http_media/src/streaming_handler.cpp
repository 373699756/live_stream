#include "http_media.h"

#include "http_flv_session.h"
#include "http_media_utils.h"
#include "http_router.h"

#include "device_media.h"
#include "infra/log.h"
#include "media/encoded_frame.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace live_stream {
namespace {

constexpr const char *kMjpegBoundary = "live_stream_frame";
constexpr const char *kMjpegFrameTail = "\r\n";

bool HasUsableFlvStartData(const MediaFlvStartData &start_data) {
    return start_data.supported && !start_data.file_header.empty() &&
           !start_data.sequence_header.empty();
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
    if (!HttpMediaStreamIdFromJsonString(stream_name, stream_id)) {
        return false;
    }
    const std::string hls_path = remaining.substr(slash + 1);
    if (!HttpMediaStartsWith(hls_path, "hls/") || hls_path.size() <= 4) {
        return false;
    }
    *object_name = hls_path.substr(4);
    return true;
}

bool IsHlsSegmentObjectName(const std::string &object_name) {
    return HttpMediaStartsWith(object_name, "seg-") && object_name.size() > 7 &&
           object_name.substr(object_name.size() - 3) == ".ts";
}

bool ParseHlsSegmentSequence(const std::string &object_name,
                             uint64_t *sequence) {
    if (sequence == nullptr || !IsHlsSegmentObjectName(object_name)) {
        return false;
    }
    const std::string sequence_text =
        object_name.substr(4, object_name.size() - 7);
    char *end = nullptr;
    const unsigned long long parsed =
        std::strtoull(sequence_text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    *sequence = static_cast<uint64_t>(parsed);
    return true;
}

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

void SendStreamingError(HttpMediaWriter *writer, ConnectionId connection_id,
                        const HttpResponse &response) {
    if (writer != nullptr) {
        writer->SendResponse(connection_id, response, true);
    }
}

class MjpegConnectionSink : public IMediaMjpegSink {
public:
    MjpegConnectionSink(HttpMediaWriter *writer, ConnectionId connection_id)
        : writer_(writer), connection_id_(connection_id) {}

    bool OnMjpegFrame(const EncodedFrame &frame) override {
        if (writer_ == nullptr) {
            return false;
        }
        const uint8_t *payload = EncodedFramePayloadData(&frame);
        if (payload == nullptr || frame.size == 0) {
            return true;
        }
        std::string frame_header;
        frame_header.reserve(128);
        frame_header.append("--");
        frame_header.append(kMjpegBoundary);
        frame_header.append("\r\nContent-Type: image/jpeg\r\nContent-Length: ");
        frame_header.append(std::to_string(frame.size));
        frame_header.append("\r\n\r\n");

        HttpMediaSlice slices[3];
        slices[0].data = reinterpret_cast<const uint8_t *>(frame_header.data());
        slices[0].size = frame_header.size();
        slices[1].data = payload;
        slices[1].size = frame.size;
        slices[1].owner = frame.buffer;
        slices[2].data = reinterpret_cast<const uint8_t *>(kMjpegFrameTail);
        slices[2].size = 2;
        return writer_->EnqueueStreamingSlices(connection_id_, slices, 3);
    }

private:
    HttpMediaWriter *writer_ = nullptr;
    ConnectionId connection_id_ = 0;
};

bool ParseFlvStreamName(const HttpRequest &request, StreamId *stream_id,
                        std::string *stream_name) {
    if (stream_id == nullptr || stream_name == nullptr) {
        return false;
    }
    *stream_name = HttpMediaPathSuffix(request.path, "/live/");
    constexpr const char *kSuffix = ".live.flv";
    constexpr size_t kSuffixSize = 9;
    if (stream_name->size() <= kSuffixSize ||
        stream_name->substr(stream_name->size() - kSuffixSize) != kSuffix) {
        return false;
    }
    stream_name->resize(stream_name->size() - kSuffixSize);
    return HttpMediaStreamIdFromJsonString(*stream_name, stream_id);
}

bool ParseMjpegStreamName(const HttpRequest &request, StreamId *stream_id,
                          std::string *stream_name) {
    if (stream_id == nullptr || stream_name == nullptr) {
        return false;
    }
    *stream_name = HttpMediaPathSuffix(request.path, "/live/");
    if (stream_name->size() <= 5 ||
        stream_name->substr(stream_name->size() - 5) != ".mjpg") {
        return false;
    }
    stream_name->resize(stream_name->size() - 5);
    return HttpMediaStreamIdFromJsonString(*stream_name, stream_id);
}

}  // namespace

class StreamingHttpHandler : public IStreamingHttpHandler {
public:
    StreamingHttpHandler(HttpAccess *access, HttpMediaWriter *writer,
                         IDeviceMedia *device_media,
                         IMediaSource *media_source,
                         IMediaFlvSource *media_flv_source,
                         IMediaMjpegSource *media_mjpeg_source)
        : access_(access), writer_(writer), device_media_(device_media),
          media_source_(media_source),
          media_flv_source_(media_flv_source),
          media_mjpeg_source_(media_mjpeg_source) {}

    bool CanHandleStreamingRequest(const HttpRequest &request) const override {
        if (request.method != HttpMethod::kGet) {
            return false;
        }
        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (ParseFlvStreamName(request, &stream_id, &stream_name) ||
            ParseMjpegStreamName(request, &stream_id, &stream_name)) {
            return true;
        }
        std::string object_name;
        return ParseHlsPath(request, &stream_id, &object_name) &&
               IsHlsSegmentObjectName(object_name);
    }

    void HandleStreamingRequest(ConnectionId connection_id,
                                const HttpRequest &request) override {
        StreamId stream_id = StreamId::kMain;
        std::string object_name;
        if (ParseHlsPath(request, &stream_id, &object_name)) {
            HandleHlsSegmentRequest(connection_id, request);
            return;
        }
        std::string stream_name;
        if (ParseMjpegStreamName(request, &stream_id, &stream_name)) {
            HandleMjpegRequest(connection_id, request);
            return;
        }
        HandleFlvRequest(connection_id, request);
    }

private:
    void HandleHlsSegmentRequest(ConnectionId connection_id,
                                 const HttpRequest &request) {
        if (media_source_ == nullptr) {
            Error(kHttpMediaModuleName,
                            "HLS reject conn=%llu reason=no_media_source",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(501, "Not Implemented"));
            return;
        }
        if (IsHttpMediaRestarting(device_media_)) {
            Error(kHttpMediaModuleName,
                            "HLS reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503,
                                              "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaPlaybackAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            Error(kHttpMediaModuleName,
                            "HLS reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id, auth_response);
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string object_name;
        if (!ParseHlsPath(request, &stream_id, &object_name)) {
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(400, "Invalid HLS path"));
            return;
        }
        uint64_t sequence = 0;
        if (!ParseHlsSegmentSequence(object_name, &sequence)) {
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(400, "Invalid HLS segment"));
            return;
        }

        const MediaSourceStatus browser_status =
            media_source_->GetBrowserStatus(stream_id);
        if (!browser_status.hls_supported) {
            Error(kHttpMediaModuleName,
                            "HLS reject conn=%llu stream=%s object=%s "
                            "reason=unsupported codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            object_name.c_str(),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(409, "HLS requires H.264 or H.265 stream"));
            return;
        }
        if (!browser_status.running) {
            Error(kHttpMediaModuleName,
                            "HLS reject conn=%llu stream=%s object=%s "
                            "reason=not_ready codec=%s running=%d hls_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            object_name.c_str(),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.hls_ready ? 1 : 0);
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503, "HLS playlist not ready"));
            return;
        }

        MediaSegmentRef segment =
            media_source_->GetHlsSegmentRef(stream_id, sequence);
        if (!segment.found || segment.body == nullptr ||
            segment.body->data == nullptr || segment.body->size == 0) {
            Error(kHttpMediaModuleName,
                            "HLS reject conn=%llu stream=%s object=%s "
                            "reason=segment_missing sequence=%llu",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            object_name.c_str(),
                            static_cast<unsigned long long>(sequence));
            MediaSegmentRefUnref(&segment);
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(404, "HLS segment not found"));
            return;
        }

        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "video/mp2t";
        HttpMediaSlice body_slice;
        body_slice.data = segment.body->data;
        body_slice.size = segment.body->size;
        body_slice.owner = segment.body;
        const bool sent = writer_ != nullptr &&
                          writer_->SendResponseSlices(
                              connection_id, response, &body_slice, 1,
                              body_slice.size, true);
        MediaSegmentRefUnref(&segment);
        if (!sent && writer_ != nullptr) {
            writer_->CloseConnection(connection_id);
        }
    }

    void HandleFlvRequest(ConnectionId connection_id,
                          const HttpRequest &request) {
        if (media_source_ == nullptr || media_flv_source_ == nullptr) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=no_media_source",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(501, "Not Implemented"));
            return;
        }
        if (IsHttpMediaRestarting(device_media_)) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503,
                                              "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaPlaybackAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id, auth_response);
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseFlvStreamName(request, &stream_id, &stream_name)) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(400, "Invalid FLV path"));
            return;
        }

        IMediaSource *media_source = media_source_;
        IMediaFlvSource *media_flv_source = media_flv_source_;
        const MediaSourceStatus browser_status =
            media_source->GetBrowserStatus(stream_id);
        if (!browser_status.flv_supported) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s "
                            "reason=unsupported codec=%s running=%d flv_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(409, "HTTP-FLV requires H.264/H.265 stream"));
            return;
        }
        if (!browser_status.running) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s reason=not_ready "
                            "codec=%s running=%d flv_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503, "FLV stream not ready"));
            return;
        }

        MediaFlvStartData start_data =
            media_source->GetFlvStartData(stream_id);
        Info(kHttpMediaModuleName,
                       "HTTP-FLV start-data conn=%llu stream=%s supported=%d "
                       "file=%zu sequence=%zu cached_flv=%zu gop_complete=%d "
                       "generation=%llu",
                       static_cast<unsigned long long>(connection_id),
                       HttpMediaStreamIdToJsonString(stream_id),
                       start_data.supported ? 1 : 0,
                       start_data.file_header.size(),
                       start_data.sequence_header.size(),
                       start_data.cached_video_tags.size(),
                       start_data.cached_gop_complete ? 1 : 0,
                       static_cast<unsigned long long>(
                           start_data.config_generation));
        if (!HasUsableFlvStartData(start_data)) {
            const bool keyframe_requested =
                RequestBrowserKeyFrame(media_source, stream_id);
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s "
                            "reason=start_data codec=%s running=%d flv_ready=%d "
                            "file=%zu sequence=%zu cached_flv=%zu "
                            "gop_complete=%d "
                            "keyframe=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0,
                            start_data.file_header.size(),
                            start_data.sequence_header.size(),
                            start_data.cached_video_tags.size(),
                            start_data.cached_gop_complete ? 1 : 0,
                            keyframe_requested ? 1 : 0);
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503, "FLV stream not ready"));
            MediaFlvStartDataUnref(&start_data);
            return;
        }

        HttpFlvSession *stream =
            new HttpFlvSession(writer_, connection_id, stream_id);
        size_t cached_flv_bytes = 0;
        const HttpFlvSessionStartStatus start_status =
            stream->Start(start_data, &cached_flv_bytes);
        if (start_status != HttpFlvSessionStartStatus::kStarted) {
            delete stream;
            Error(kHttpMediaModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=%s",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            HttpFlvSessionStartStatusName(start_status));
            if (writer_ != nullptr &&
                HttpFlvSessionStartNeedsClose(start_status)) {
                writer_->CloseConnection(connection_id);
            }
            MediaFlvStartDataUnref(&start_data);
            return;
        }

        const bool wait_for_keyframe =
            start_data.cached_video_tags.empty() ||
            !start_data.cached_gop_complete;
        const MediaFlvClientId client_id = media_flv_source->AttachFlvClient(
            stream_id, start_data.config_generation, wait_for_keyframe,
            stream);
        if (client_id == 0) {
            delete stream;
            Error(kHttpMediaModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            MediaFlvStartDataUnref(&start_data);
            return;
        }

        HttpMediaClientHandle client;
        client.type = HttpMediaClientType::kFlv;
        client.id = client_id;
        if (!writer_->AttachStreamClient(connection_id, client)) {
            (void)media_flv_source->DetachFlvClient(client_id);
            Error(kHttpMediaModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id));
            MediaFlvStartDataUnref(&start_data);
            return;
        }
        Info(kHttpMediaModuleName,
                       "HTTP-FLV attached conn=%llu stream=%s client=%llu "
                       "wait_keyframe=%d request_keyframe=1 cached_flv=%zu "
                       "cached_bytes=%zu gop_complete=%d",
                       static_cast<unsigned long long>(connection_id),
                       HttpMediaStreamIdToJsonString(stream_id),
                       static_cast<unsigned long long>(client_id),
                       wait_for_keyframe ? 1 : 0,
                       start_data.cached_video_tags.size(), cached_flv_bytes,
                       start_data.cached_gop_complete ? 1 : 0);
        MediaFlvStartDataUnref(&start_data);
    }

    void HandleMjpegRequest(ConnectionId connection_id,
                            const HttpRequest &request) {
        if (media_source_ == nullptr ||
            media_mjpeg_source_ == nullptr) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=no_media_source",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(501, "Not Implemented"));
            return;
        }
        if (IsHttpMediaRestarting(device_media_)) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503,
                                              "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaPlaybackAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id, auth_response);
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseMjpegStreamName(request, &stream_id, &stream_name)) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(400, "Invalid MJPEG path"));
            return;
        }

        const MediaSourceStatus browser_status =
            media_source_->GetBrowserStatus(stream_id);
        if (!browser_status.mjpeg_supported) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu stream=%s "
                            "reason=unsupported codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(409, "MJPEG preview requires MJPEG stream"));
            return;
        }
        if (!browser_status.mjpeg_ready) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu stream=%s "
                            "reason=not_ready codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            SendStreamingError(writer_, connection_id,
                               HttpMediaTextResponse(503, "MJPEG stream not ready"));
            return;
        }

        IMediaMjpegSink *sink =
            new MjpegConnectionSink(writer_, connection_id);
        if (writer_ == nullptr || !writer_->BeginStream(connection_id)) {
            delete sink;
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s "
                            "reason=no_session",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id));
            return;
        }

        std::map<std::string, std::string> headers;
        headers["Content-Type"] =
            std::string("multipart/x-mixed-replace; boundary=") +
            kMjpegBoundary;
        headers["Cache-Control"] = "no-cache";
        headers["Pragma"] = "no-cache";
        const std::string header_block = BuildHttpMediaStreamingHeaderBlock(200, headers);
        if (!writer_->EnqueueStreamingChunk(
                connection_id,
                reinterpret_cast<const uint8_t *>(header_block.data()),
                header_block.size())) {
            delete sink;
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=enqueue",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }

        const MediaMjpegClientId client_id =
            media_mjpeg_source_->AttachMjpegClient(stream_id, sink);
        if (client_id == 0) {
            delete sink;
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }

        HttpMediaClientHandle client;
        client.type = HttpMediaClientType::kMjpeg;
        client.id = client_id;
        if (!writer_->AttachStreamClient(connection_id, client)) {
            (void)media_mjpeg_source_->DetachMjpegClient(client_id);
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            HttpMediaStreamIdToJsonString(stream_id));
            return;
        }
        Info(kHttpMediaModuleName,
                       "HTTP-MJPEG attached conn=%llu stream=%s client=%llu",
                       static_cast<unsigned long long>(connection_id),
                       HttpMediaStreamIdToJsonString(stream_id),
                       static_cast<unsigned long long>(client_id));
    }

    HttpAccess *access_ = nullptr;
    HttpMediaWriter *writer_ = nullptr;
    IDeviceMedia *device_media_ = nullptr;
    IMediaSource *media_source_ = nullptr;
    IMediaFlvSource *media_flv_source_ = nullptr;
    IMediaMjpegSource *media_mjpeg_source_ = nullptr;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpAccess *access, HttpMediaWriter *writer, IDeviceMedia *device_media,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source) {
    return std::unique_ptr<IStreamingHttpHandler>(
        new StreamingHttpHandler(access, writer, device_media,
                                 media_source, media_flv_source,
                                 media_mjpeg_source));
}

}  // namespace live_stream
