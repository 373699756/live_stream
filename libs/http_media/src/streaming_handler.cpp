#include "http_media.h"

#include "event_stream.h"
#include "http_flv_session.h"
#include "http_media_utils.h"
#include "http_router.h"

#include "device_media.h"
#include "event.h"
#include "infra/log.h"
#include "media/encoded_frame.h"

#include <cstddef>
#include <cstdint>
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

HttpStreamingRequestResult SendStreamingError(
    HttpMediaWriter *writer, ConnectionId connection_id,
    const HttpResponse &response) {
    if (writer == nullptr) {
        return HttpStreamingRequestResult::kFailed;
    }
    writer->SendResponse(connection_id, response, true);
    return HttpStreamingRequestResult::kResponseSent;
}

HttpStreamingRequestResult CloseStreamingConnection(
    HttpMediaWriter *writer, ConnectionId connection_id) {
    if (writer == nullptr) {
        return HttpStreamingRequestResult::kFailed;
    }
    writer->CloseConnection(connection_id);
    return HttpStreamingRequestResult::kClosed;
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

        MediaSlice slices[3];
        slices[0].data = reinterpret_cast<const uint8_t *>(frame_header.data());
        slices[0].size = frame_header.size();
        slices[1].data = payload;
        slices[1].size = frame.size;
        // JPEG payload 不复制进 HTTP 层；owner 保证异步发送期间 frame.buffer 存活。
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
    return MediaStreamIdFromJson(*stream_name, stream_id);
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
    return MediaStreamIdFromJson(*stream_name, stream_id);
}

}  // namespace

class StreamingHttpHandler : public IStreamingHttpHandler {
public:
    StreamingHttpHandler(HttpAccess *access, HttpMediaWriter *writer,
                         IDeviceMedia *device_media,
                         IMediaSource *media_source,
                         IMediaFlvSource *media_flv_source,
                         IMediaMjpegSource *media_mjpeg_source,
                         IEvent *event)
        : access_(access), writer_(writer), device_media_(device_media),
          media_source_(media_source),
          media_flv_source_(media_flv_source),
          media_mjpeg_source_(media_mjpeg_source),
          event_(event) {}

    bool CanHandleStreamingRequest(const HttpRequest &request) const override {
        if (request.method != HttpMethod::kGet) {
            return false;
        }
        // 只有真正需要直接占用 TCP 连接的入口走 streaming path。
        // HLS playlist/segment 都是短响应，由普通 HTTP route 发送。
        if (request.path == kEventStreamPath) {
            return true;
        }
        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        return ParseFlvStreamName(request, &stream_id, &stream_name) ||
               ParseMjpegStreamName(request, &stream_id, &stream_name);
    }

    HttpStreamingRequestResult HandleStreamingRequest(
        ConnectionId connection_id, const HttpRequest &request) override {
        // 分发顺序先匹配最明确的 SSE/MJPEG，最后才按 FLV 处理；
        // 这样非法 /live/... 路径不会被误当成 FLV。
        if (request.path == kEventStreamPath) {
            return HandleEventStreamRequest(connection_id, request);
        }
        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (ParseMjpegStreamName(request, &stream_id, &stream_name)) {
            return HandleMjpegRequest(connection_id, request);
        }
        return HandleFlvRequest(connection_id, request);
    }

private:
    HttpStreamingRequestResult HandleEventStreamRequest(
        ConnectionId connection_id, const HttpRequest &request) {
        if (event_ == nullptr || writer_ == nullptr) {
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(501, "Not Implemented"));
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireHttpMediaAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return SendStreamingError(writer_, connection_id, auth_response);
        }
        // SSE 是无 Content-Length 的长连接，必须先把 HttpSession 切成 streaming；
        // 之后断连时由 HTTP close callback 自动 Unsubscribe。
        if (!writer_->BeginStream(connection_id,
                                  HttpMediaClientType::kEventStream,
                                  StreamId::kMain)) {
            return CloseStreamingConnection(writer_, connection_id);
        }

        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "text/event-stream";
        headers["Cache-Control"] = "no-cache";
        headers["Pragma"] = "no-cache";
        headers["X-Accel-Buffering"] = "no";
        const std::string header_block =
            BuildHttpStreamHeaderBlock(200, headers);
        if (!writer_->EnqueueStreamingChunk(
                connection_id,
                reinterpret_cast<const uint8_t *>(header_block.data()),
                header_block.size())) {
            return CloseStreamingConnection(writer_, connection_id);
        }

        const std::string hello = BuildEventStreamHello();
        if (!writer_->EnqueueStreamingChunk(
                connection_id, reinterpret_cast<const uint8_t *>(hello.data()),
                hello.size())) {
            return CloseStreamingConnection(writer_, connection_id);
        }

        const EventSubscriptionId subscription_id = event_->Subscribe(
            EventType::kMediaStatusChanged,
            [writer = writer_, connection_id](const Event &event) {
                if (writer == nullptr) {
                    return;
                }
                const std::string message = BuildEventStreamMessage(event);
                // SSE 推送失败说明 TCP 队列关闭或客户端过慢，直接关连接，
                // 后续 HTTP close callback 会取消订阅。
                if (!writer->EnqueueStreamingChunk(
                        connection_id,
                        reinterpret_cast<const uint8_t *>(message.data()),
                        message.size())) {
                    writer->CloseConnection(connection_id);
                }
            });
        if (subscription_id == 0) {
            return CloseStreamingConnection(writer_, connection_id);
        }
        HttpMediaClientHandle client;
        client.type = HttpMediaClientType::kEventStream;
        client.id = subscription_id;
        client.stream_id = StreamId::kMain;
        // AttachStreamClient 把 event subscription id 交给 HTTP session，
        // 连接异常关闭时才能从统一 close path 解除订阅。
        if (!writer_->AttachStreamClient(connection_id, client)) {
            (void)event_->Unsubscribe(subscription_id);
            return CloseStreamingConnection(writer_, connection_id);
        }
        return HttpStreamingRequestResult::kStreaming;
    }

    HttpStreamingRequestResult HandleFlvRequest(
        ConnectionId connection_id, const HttpRequest &request) {
        if (media_source_ == nullptr || media_flv_source_ == nullptr) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=no_media_source",
                            static_cast<unsigned long long>(connection_id));
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(501, "Not Implemented"));
        }
        if (IsHttpMediaRestarting(device_media_)) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(503, "Media pipeline restarting"));
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequirePlaybackAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            return SendStreamingError(writer_, connection_id, auth_response);
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseFlvStreamName(request, &stream_id, &stream_name)) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(400, "Invalid FLV path"));
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
                            MediaStreamIdToJson(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(409, "HTTP-FLV requires H.264/H.265 stream"));
        }
        if (!browser_status.running) {
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s reason=not_ready "
                            "codec=%s running=%d flv_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(503, "FLV stream not ready"));
        }

        // 新 FLV 客户端必须先拿 file header、sequence header 和当前 GOP，
        // 浏览器才能在不等待下一轮 SPS/PPS 的情况下尽快解码。
        MediaFlvStartData start_data =
            media_source->GetFlvStartData(stream_id);
        Info(kHttpMediaModuleName,
                       "HTTP-FLV start-data conn=%llu stream=%s supported=%d "
                       "file=%zu sequence=%zu cached_flv=%zu gop_complete=%d "
                       "generation=%llu",
                       static_cast<unsigned long long>(connection_id),
                       MediaStreamIdToJson(stream_id),
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
            // start data 不完整时不创建 FLV client，只请求关键帧让 media_source 尽快
            // 生成新的 sequence header/GOP，客户端需要重新发起请求。
            Error(kHttpMediaModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s "
                            "reason=start_data codec=%s running=%d flv_ready=%d "
                            "file=%zu sequence=%zu cached_flv=%zu "
                            "gop_complete=%d "
                            "keyframe=%d",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0,
                            start_data.file_header.size(),
                            start_data.sequence_header.size(),
                            start_data.cached_video_tags.size(),
                            start_data.cached_gop_complete ? 1 : 0,
                            keyframe_requested ? 1 : 0);
            MediaFlvStartDataUnref(&start_data);
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(503, "FLV stream not ready"));
        }

        HttpFlvSession *stream =
            new HttpFlvSession(writer_, connection_id, stream_id);
        size_t cached_flv_bytes = 0;
        const HttpFlvSessionStartStatus start_status =
            stream->Start(start_data, &cached_flv_bytes);
        if (start_status != HttpFlvSessionStartStatus::kStarted) {
            // Start() 失败时 stream 尚未 attach 到 media_source，调用方仍拥有对象。
            delete stream;
            Error(kHttpMediaModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=%s",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id),
                            HttpFlvSessionStartStatusName(start_status));
            if (writer_ != nullptr &&
                HttpFlvSessionStartNeedsClose(start_status)) {
                writer_->CloseConnection(connection_id);
                MediaFlvStartDataUnref(&start_data);
                return HttpStreamingRequestResult::kClosed;
            }
            MediaFlvStartDataUnref(&start_data);
            return start_status == HttpFlvSessionStartStatus::kNoSession
                       ? HttpStreamingRequestResult::kFailed
                       : HttpStreamingRequestResult::kClosed;
        }

        // GOP 不完整时 live attach 必须等下一个关键帧，否则客户端会从 P/B 帧开始
        // 导致首屏花屏或解码器长时间等待参考帧。
        const bool wait_for_keyframe =
            start_data.cached_video_tags.empty() ||
            !start_data.cached_gop_complete;
        const MediaFlvClientId client_id = media_flv_source->AttachFlvClient(
            stream_id, start_data.config_generation, wait_for_keyframe,
            stream);
        if (client_id == 0) {
            // attach 失败后 media_source 不会接管 stream 生命周期，必须在这里删除。
            delete stream;
            Error(kHttpMediaModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id));
            writer_->CloseConnection(connection_id);
            MediaFlvStartDataUnref(&start_data);
            return HttpStreamingRequestResult::kClosed;
        }

        HttpMediaClientHandle client;
        client.type = HttpMediaClientType::kFlv;
        client.id = client_id;
        client.stream_id = stream_id;
        // FLV client id 挂到 HTTP session，TCP close 时统一 DetachFlvClient，
        // 不能靠浏览器主动关闭请求来释放媒体 fanout。
        if (!writer_->AttachStreamClient(connection_id, client)) {
            (void)media_flv_source->DetachFlvClient(client_id);
            // DetachFlvClient 会释放 attach 时交给 media_source 的 HttpFlvSession。
            Error(kHttpMediaModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id));
            MediaFlvStartDataUnref(&start_data);
            writer_->CloseConnection(connection_id);
            return HttpStreamingRequestResult::kClosed;
        }
        const bool keyframe_requested =
            RequestBrowserKeyFrame(media_source, stream_id);
        Info(kHttpMediaModuleName,
                       "HTTP-FLV attached conn=%llu stream=%s client=%llu "
                       "wait_keyframe=%d request_keyframe=%d cached_flv=%zu "
                       "cached_bytes=%zu gop_complete=%d",
                       static_cast<unsigned long long>(connection_id),
                       MediaStreamIdToJson(stream_id),
                       static_cast<unsigned long long>(client_id),
                       wait_for_keyframe ? 1 : 0,
                       keyframe_requested ? 1 : 0,
                       start_data.cached_video_tags.size(), cached_flv_bytes,
                       start_data.cached_gop_complete ? 1 : 0);
        MediaFlvStartDataUnref(&start_data);
        return HttpStreamingRequestResult::kStreaming;
    }

    HttpStreamingRequestResult HandleMjpegRequest(
        ConnectionId connection_id, const HttpRequest &request) {
        if (media_source_ == nullptr ||
            media_mjpeg_source_ == nullptr) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=no_media_source",
                            static_cast<unsigned long long>(connection_id));
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(501, "Not Implemented"));
        }
        if (IsHttpMediaRestarting(device_media_)) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(503, "Media pipeline restarting"));
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequirePlaybackAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            return SendStreamingError(writer_, connection_id, auth_response);
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseMjpegStreamName(request, &stream_id, &stream_name)) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(400, "Invalid MJPEG path"));
        }

        const MediaSourceStatus browser_status =
            media_source_->GetBrowserStatus(stream_id);
        if (!browser_status.mjpeg_supported) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu stream=%s "
                            "reason=unsupported codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(409, "MJPEG preview requires MJPEG stream"));
        }
        if (!browser_status.mjpeg_ready) {
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG reject conn=%llu stream=%s "
                            "reason=not_ready codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            return SendStreamingError(
                writer_, connection_id,
                HttpMediaTextResponse(503, "MJPEG stream not ready"));
        }

        // MJPEG sink 生命周期归 media_source；HTTP session 只保存 client id。
        // AttachStreamClient 失败时必须立刻 detach，避免 sink 泄漏。
        IMediaMjpegSink *sink =
            new MjpegConnectionSink(writer_, connection_id);
        if (writer_ == nullptr ||
            !writer_->BeginStream(connection_id, HttpMediaClientType::kMjpeg,
                                  stream_id)) {
            delete sink;
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s "
                            "reason=no_session",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id));
            return HttpStreamingRequestResult::kFailed;
        }

        std::map<std::string, std::string> headers;
        headers["Content-Type"] =
            std::string("multipart/x-mixed-replace; boundary=") +
            kMjpegBoundary;
        headers["Cache-Control"] = "no-cache";
        headers["Pragma"] = "no-cache";
        const std::string header_block = BuildHttpStreamHeaderBlock(200, headers);
        if (!writer_->EnqueueStreamingChunk(
                connection_id,
                reinterpret_cast<const uint8_t *>(header_block.data()),
                header_block.size())) {
            delete sink;
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=enqueue",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id));
            writer_->CloseConnection(connection_id);
            return HttpStreamingRequestResult::kClosed;
        }

        const MediaMjpegClientId client_id =
            media_mjpeg_source_->AttachMjpegClient(stream_id, sink);
        if (client_id == 0) {
            // attach 失败时 sink 仍归当前函数所有。
            delete sink;
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id));
            writer_->CloseConnection(connection_id);
            return HttpStreamingRequestResult::kClosed;
        }

        HttpMediaClientHandle client;
        client.type = HttpMediaClientType::kMjpeg;
        client.id = client_id;
        client.stream_id = stream_id;
        if (!writer_->AttachStreamClient(connection_id, client)) {
            (void)media_mjpeg_source_->DetachMjpegClient(client_id);
            Error(kHttpMediaModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            MediaStreamIdToJson(stream_id));
            writer_->CloseConnection(connection_id);
            return HttpStreamingRequestResult::kClosed;
        }
        Info(kHttpMediaModuleName,
                       "HTTP-MJPEG attached conn=%llu stream=%s client=%llu",
                       static_cast<unsigned long long>(connection_id),
                       MediaStreamIdToJson(stream_id),
                       static_cast<unsigned long long>(client_id));
        return HttpStreamingRequestResult::kStreaming;
    }

    HttpAccess *access_ = nullptr;
    HttpMediaWriter *writer_ = nullptr;
    IDeviceMedia *device_media_ = nullptr;
    IMediaSource *media_source_ = nullptr;
    IMediaFlvSource *media_flv_source_ = nullptr;
    IMediaMjpegSource *media_mjpeg_source_ = nullptr;
    IEvent *event_ = nullptr;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    const StreamingHttpHandlerDependencies &dependencies) {
    return std::unique_ptr<IStreamingHttpHandler>(
        new StreamingHttpHandler(
            dependencies.access, dependencies.writer,
            dependencies.device_media, dependencies.media_source,
            dependencies.media_flv_source,
            dependencies.media_mjpeg_source, dependencies.event));
}

}  // namespace live_stream
