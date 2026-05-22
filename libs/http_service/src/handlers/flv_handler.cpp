#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
#include "net_service.h"
#include "stream_hub_service.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace live_stream {
namespace {

constexpr size_t kFlvTagHeaderSize = 11;
constexpr size_t kFlvPreviousTagSize = 4;
constexpr size_t kFlvVideoBodyOffset = kFlvTagHeaderSize;
constexpr size_t kFlvH264PacketTypeOffset = kFlvVideoBodyOffset + 1;
constexpr uint8_t kFlvTagTypeVideo = 9;
constexpr uint8_t kFlvH264PacketTypeSequenceHeader = 0;
constexpr uint8_t kFlvH264PacketTypeCodedFrames = 1;

bool HasUsableFlvStartData(const StreamFlvStartData &start_data) {
    return start_data.supported && !start_data.file_header.empty() &&
           !start_data.sequence_header.empty();
}

uint32_t ReadU24(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) |
           static_cast<uint32_t>(data[2]);
}

uint32_t ReadFlvTimestampMs(const uint8_t *data) {
    return (static_cast<uint32_t>(data[7]) << 24) |
           (static_cast<uint32_t>(data[4]) << 16) |
           (static_cast<uint32_t>(data[5]) << 8) |
           static_cast<uint32_t>(data[6]);
}

void WriteFlvTimestampMs(uint32_t timestamp_ms, uint8_t *data) {
    data[4] = static_cast<uint8_t>((timestamp_ms >> 16) & 0xff);
    data[5] = static_cast<uint8_t>((timestamp_ms >> 8) & 0xff);
    data[6] = static_cast<uint8_t>(timestamp_ms & 0xff);
    data[7] = static_cast<uint8_t>((timestamp_ms >> 24) & 0xff);
}

bool IsCompleteFlvVideoTag(const uint8_t *data, size_t size,
                           uint32_t *body_size) {
    if (data == nullptr || body_size == nullptr ||
        size < kFlvTagHeaderSize + kFlvPreviousTagSize ||
        data[0] != kFlvTagTypeVideo) {
        return false;
    }
    *body_size = ReadU24(data + 1);
    return size >= kFlvTagHeaderSize + *body_size + kFlvPreviousTagSize;
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

bool RequestBrowserKeyFrame(IStreamHubService *stream_hub_service,
                            StreamId stream_id) {
    return stream_hub_service != nullptr &&
           stream_hub_service->RequestKeyFrame(stream_id,
                                               KeyFrameReason::kNewClient);
}

class FlvConnectionSink : public IStreamFlvSink {
public:
    FlvConnectionSink(HttpHandlerContext *context, ConnectionId connection_id)
        : context_(context), connection_id_(connection_id) {}

    bool OnFlvChunk(const uint8_t *data, size_t size) override {
        if (context_ == nullptr || data == nullptr || size == 0) {
            return context_ != nullptr;
        }

        uint32_t body_size = 0;
        if (!IsCompleteFlvVideoTag(data, size, &body_size)) {
            return context_->EnqueueStreamingChunk(connection_id_, data, size);
        }

        std::shared_ptr<std::string> chunk(new std::string(
            reinterpret_cast<const char *>(data), size));
        uint8_t *tag = reinterpret_cast<uint8_t *>(&(*chunk)[0]);
        const uint32_t timestamp_ms = ReadFlvTimestampMs(tag);
        const uint8_t packet_type =
            size > kFlvH264PacketTypeOffset ? tag[kFlvH264PacketTypeOffset]
                                            : 0xff;
        if (packet_type == kFlvH264PacketTypeSequenceHeader) {
            WriteFlvTimestampMs(0, tag);
            return context_->EnqueueStreamingChunk(connection_id_, chunk);
        }
        if (!timestamp_base_set_) {
            timestamp_base_ms_ = timestamp_ms;
            timestamp_base_set_ = true;
            INFRA_LOG_INFO(kHttpModuleName,
                           "HTTP-FLV timestamp base conn=%llu base_ms=%u",
                           static_cast<unsigned long long>(connection_id_),
                           timestamp_base_ms_);
        }
        uint32_t rebased_ms =
            timestamp_ms >= timestamp_base_ms_ ? timestamp_ms - timestamp_base_ms_
                                               : last_timestamp_ms_;
        if (timestamp_base_set_ && rebased_ms < last_timestamp_ms_ &&
            packet_type == kFlvH264PacketTypeCodedFrames) {
            rebased_ms = last_timestamp_ms_;
        }
        last_timestamp_ms_ = rebased_ms;
        WriteFlvTimestampMs(rebased_ms, tag);
        return context_->EnqueueStreamingChunk(connection_id_, chunk);
    }

private:
    HttpHandlerContext *context_ = nullptr;
    ConnectionId connection_id_ = 0;
    bool timestamp_base_set_ = false;
    uint32_t timestamp_base_ms_ = 0;
    uint32_t last_timestamp_ms_ = 0;
};

void SendFlvError(HttpHandlerContext *context, ConnectionId connection_id,
                  const HttpResponse &response) {
    context->SendResponse(connection_id, response, true);
}

bool ParseFlvStreamName(const HttpRequest &request, StreamId *stream_id,
                        std::string *stream_name) {
    if (stream_id == nullptr || stream_name == nullptr) {
        return false;
    }
    *stream_name = PathSuffix(request.path, "/api/flv/");
    if (stream_name->size() <= 4 ||
        stream_name->substr(stream_name->size() - 4) != ".flv") {
        return false;
    }
    stream_name->resize(stream_name->size() - 4);
    return StreamIdFromJsonString(*stream_name, stream_id);
}

}  // namespace

class StreamingHttpHandler : public IStreamingHttpHandler {
public:
    StreamingHttpHandler(HttpHandlerContext *context,
                         const MediaHandlerDependencies &dependencies)
        : context_(context), dependencies_(dependencies) {}

    bool CanHandleStreamingRequest(const HttpRequest &request) const override {
        return request.method == HttpMethod::kGet &&
               StartsWith(request.path, "/api/flv/");
    }

    void HandleStreamingRequest(ConnectionId connection_id,
                                const HttpRequest &request) override {
        if (dependencies_.stream_hub_service == nullptr) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=no_stream_hub",
                            static_cast<unsigned long long>(connection_id));
            SendFlvError(context_, connection_id,
                         StatusResponse(501, "Not Implemented"));
            return;
        }
        if (IsMediaRestarting(dependencies_.media_service)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendFlvError(context_, connection_id,
                         StatusResponse(503, "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        if (!RequireAuth(context_, request, &principal)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendFlvError(context_, connection_id,
                         StatusResponse(401, "Unauthorized"));
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseFlvStreamName(request, &stream_id, &stream_name)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            SendFlvError(context_, connection_id,
                         StatusResponse(400, "Invalid FLV path"));
            return;
        }

        IStreamHubService *stream_hub = dependencies_.stream_hub_service;
        const StreamBrowserStatus browser_status =
            stream_hub->GetBrowserStatus(stream_id);
        if (!browser_status.flv_supported) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s "
                            "reason=unsupported codec=%s running=%d flv_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            SendFlvError(context_, connection_id,
                         StatusResponse(409, "HTTP-FLV requires H.264 stream"));
            return;
        }
        if (!browser_status.running) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s reason=not_ready "
                            "codec=%s running=%d flv_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            SendFlvError(context_, connection_id,
                         StatusResponse(503, "FLV stream not ready"));
            return;
        }

        StreamFlvStartData start_data = stream_hub->GetFlvStartData(stream_id);
        INFRA_LOG_INFO(kHttpModuleName,
                       "HTTP-FLV start-data conn=%llu stream=%s supported=%d "
                       "file=%zu sequence=%zu cached_keyframe=%zu "
                       "generation=%llu",
                       static_cast<unsigned long long>(connection_id),
                       StreamIdToJsonString(stream_id),
                       start_data.supported ? 1 : 0,
                       start_data.file_header.size(),
                       start_data.sequence_header.size(),
                       start_data.last_keyframe.size(),
                       static_cast<unsigned long long>(
                           start_data.config_generation));
        if (!HasUsableFlvStartData(start_data)) {
            const bool keyframe_requested =
                RequestBrowserKeyFrame(stream_hub, stream_id);
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s "
                            "reason=start_data codec=%s running=%d flv_ready=%d "
                            "file=%zu sequence=%zu cached_keyframe=%zu "
                            "keyframe=%d",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0,
                            start_data.file_header.size(),
                            start_data.sequence_header.size(),
                            start_data.last_keyframe.size(),
                            keyframe_requested ? 1 : 0);
            SendFlvError(context_, connection_id,
                         StatusResponse(503, "FLV stream not ready"));
            return;
        }

        std::shared_ptr<IStreamFlvSink> sink(
            new FlvConnectionSink(context_, connection_id));
        if (!context_->BeginFlvSession(connection_id, sink)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=no_session",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            return;
        }

        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "video/x-flv";
        headers["Cache-Control"] = "no-cache";
        headers["Pragma"] = "no-cache";
        const std::string header_block = BuildStreamingHeaderBlock(200, headers);
        INFRA_LOG_INFO(kHttpModuleName,
                       "HTTP-FLV start conn=%llu stream=%s client=%llu header=%zu "
                       "file=%zu sequence=%zu cached_keyframe=%zu",
                       static_cast<unsigned long long>(connection_id),
                       StreamIdToJsonString(stream_id),
                       static_cast<unsigned long long>(0), header_block.size(),
                       start_data.file_header.size(),
                       start_data.sequence_header.size(),
                       start_data.last_keyframe.size());
        std::string start_block;
        start_block.reserve(header_block.size() + start_data.file_header.size());
        start_block.append(header_block);
        start_block.append(start_data.file_header);
        if (!context_->EnqueueStreamingChunk(
                connection_id,
                reinterpret_cast<const uint8_t *>(start_block.data()),
                start_block.size())) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=enqueue",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            context_->CloseConnection(connection_id);
            return;
        }
        if (!sink->OnFlvChunk(
                reinterpret_cast<const uint8_t *>(
                    start_data.sequence_header.data()),
                start_data.sequence_header.size())) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=start_tags",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            context_->CloseConnection(connection_id);
            return;
        }

        const bool wait_for_keyframe = true;
        const StreamFlvClientId client_id = stream_hub->AttachFlvClient(
            stream_id, start_data.config_generation, wait_for_keyframe, sink);
        if (client_id == 0) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            context_->CloseConnection(connection_id);
            return;
        }

        if (!context_->AttachFlvSessionClient(connection_id, client_id)) {
            (void)stream_hub->DetachFlvClient(client_id);
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            return;
        }
        INFRA_LOG_INFO(kHttpModuleName,
                       "HTTP-FLV attached conn=%llu stream=%s client=%llu "
                       "wait_keyframe=%d request_keyframe=1",
                       static_cast<unsigned long long>(connection_id),
                       StreamIdToJsonString(stream_id),
                       static_cast<unsigned long long>(client_id),
                       wait_for_keyframe ? 1 : 0);
    }

private:
    HttpHandlerContext *context_ = nullptr;
    MediaHandlerDependencies dependencies_;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpHandlerContext *context,
    const MediaHandlerDependencies &dependencies) {
    return std::unique_ptr<IStreamingHttpHandler>(
        new StreamingHttpHandler(context, dependencies));
}

}  // namespace live_stream
