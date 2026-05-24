#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
#include "net_service.h"
#include "stream_browser_source.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace live_stream {
namespace {

constexpr size_t kFlvTagHeaderSize = 11;
constexpr size_t kFlvPreviousTagSize = 4;
constexpr size_t kFlvVideoBodyOffset = kFlvTagHeaderSize;
constexpr size_t kFlvH264PacketTypeOffset = kFlvVideoBodyOffset + 1;
constexpr uint8_t kFlvTagTypeVideo = 9;
constexpr uint8_t kFlvEnhancedHeader = 0x80;
constexpr uint8_t kFlvPacketTypeSequenceHeader = 0;
constexpr uint8_t kFlvPacketTypeCodedFrames = 1;

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

uint8_t ReadFlvVideoPacketType(const uint8_t *data, size_t size) {
    if (data == nullptr || size <= kFlvVideoBodyOffset) {
        return 0xff;
    }
    const uint8_t video_header = data[kFlvVideoBodyOffset];
    if ((video_header & kFlvEnhancedHeader) != 0) {
        return static_cast<uint8_t>(video_header & 0x0f);
    }
    if (size <= kFlvH264PacketTypeOffset) {
        return 0xff;
    }
    return data[kFlvH264PacketTypeOffset];
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

bool RequestBrowserKeyFrame(IStreamBrowserSource *stream_browser_source,
                            StreamId stream_id) {
    return stream_browser_source != nullptr &&
           stream_browser_source->RequestKeyFrame(stream_id,
                                               KeyFrameReason::kNewClient);
}

bool EnqueueFlvTagWithTimestamp(HttpStreamWriter *writer,
                                ConnectionId connection_id,
                                const uint8_t *data, size_t size,
                                uint32_t timestamp_ms) {
    if (writer == nullptr || data == nullptr || size < kFlvTagHeaderSize) {
        return writer != nullptr;
    }
    uint8_t header[kFlvTagHeaderSize];
    std::memcpy(header, data, sizeof(header));
    WriteFlvTimestampMs(timestamp_ms, header);

    HttpStreamSlice slices[2];
    slices[0].data = header;
    slices[0].size = sizeof(header);
    slices[1].data = data + sizeof(header);
    slices[1].size = size - sizeof(header);
    return writer->EnqueueStreamingSlices(connection_id, slices, 2);
}

class FlvConnectionSink : public IStreamFlvSink {
public:
    FlvConnectionSink(HttpStreamWriter *writer, ConnectionId connection_id)
        : writer_(writer), connection_id_(connection_id) {}

    bool OnFlvChunk(const uint8_t *data, size_t size) override {
        if (writer_ == nullptr || data == nullptr || size == 0) {
            return writer_ != nullptr;
        }

        uint32_t body_size = 0;
        if (!IsCompleteFlvVideoTag(data, size, &body_size)) {
            return writer_->EnqueueStreamingChunk(connection_id_, data, size);
        }

        const uint32_t timestamp_ms = ReadFlvTimestampMs(data);
        const uint8_t packet_type = ReadFlvVideoPacketType(data, size);
        if (packet_type == kFlvPacketTypeSequenceHeader) {
            return EnqueueFlvTagWithTimestamp(writer_, connection_id_, data,
                                              size, 0);
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
            packet_type == kFlvPacketTypeCodedFrames) {
            rebased_ms = last_timestamp_ms_;
        }
        last_timestamp_ms_ = rebased_ms;
        return EnqueueFlvTagWithTimestamp(writer_, connection_id_, data, size,
                                          rebased_ms);
    }

private:
    HttpStreamWriter *writer_ = nullptr;
    ConnectionId connection_id_ = 0;
    bool timestamp_base_set_ = false;
    uint32_t timestamp_base_ms_ = 0;
    uint32_t last_timestamp_ms_ = 0;
};

void SendFlvError(HttpStreamWriter *writer, ConnectionId connection_id,
                  const HttpResponse &response) {
    writer->SendResponse(connection_id, response, true);
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
    StreamingHttpHandler(HttpAccess *access, HttpStreamWriter *writer,
                         IMediaService *media_service,
                         IStreamBrowserSource *stream_browser_source,
                         IStreamFlvSource *stream_flv_source)
        : access_(access), writer_(writer), media_service_(media_service),
          stream_browser_source_(stream_browser_source),
          stream_flv_source_(stream_flv_source) {}

    bool CanHandleStreamingRequest(const HttpRequest &request) const override {
        return request.method == HttpMethod::kGet &&
               StartsWith(request.path, "/api/flv/");
    }

    void HandleStreamingRequest(ConnectionId connection_id,
                                const HttpRequest &request) override {
        if (stream_browser_source_ == nullptr || stream_flv_source_ == nullptr) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=no_stream_hub",
                            static_cast<unsigned long long>(connection_id));
            SendFlvError(writer_, connection_id,
                         StatusResponse(501, "Not Implemented"));
            return;
        }
        if (IsMediaRestarting(media_service_)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendFlvError(writer_, connection_id,
                         StatusResponse(503, "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendFlvError(writer_, connection_id, auth_response);
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseFlvStreamName(request, &stream_id, &stream_name)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            SendFlvError(writer_, connection_id,
                         StatusResponse(400, "Invalid FLV path"));
            return;
        }

        IStreamBrowserSource *browser_source = stream_browser_source_;
        IStreamFlvSource *flv_source = stream_flv_source_;
        const StreamBrowserStatus browser_status =
            browser_source->GetBrowserStatus(stream_id);
        if (!browser_status.flv_supported) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu stream=%s "
                            "reason=unsupported codec=%s running=%d flv_ready=%d",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0,
                            browser_status.flv_ready ? 1 : 0);
            SendFlvError(writer_, connection_id,
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
            SendFlvError(writer_, connection_id,
                         StatusResponse(503, "FLV stream not ready"));
            return;
        }

        StreamFlvStartData start_data =
            browser_source->GetFlvStartData(stream_id);
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
                RequestBrowserKeyFrame(browser_source, stream_id);
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
            SendFlvError(writer_, connection_id,
                         StatusResponse(503, "FLV stream not ready"));
            return;
        }

        IStreamFlvSink *sink = new FlvConnectionSink(writer_, connection_id);
        if (writer_ == nullptr || !writer_->BeginStream(connection_id)) {
            delete sink;
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
        if (!writer_->EnqueueStreamingChunk(
                connection_id,
                reinterpret_cast<const uint8_t *>(start_block.data()),
                start_block.size())) {
            delete sink;
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=enqueue",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }
        if (!sink->OnFlvChunk(
                reinterpret_cast<const uint8_t *>(
                    start_data.sequence_header.data()),
                start_data.sequence_header.size())) {
            delete sink;
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=start_tags",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }

        const bool wait_for_keyframe = true;
        const StreamFlvClientId client_id = flv_source->AttachFlvClient(
            stream_id, start_data.config_generation, wait_for_keyframe, sink);
        if (client_id == 0) {
            delete sink;
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }

        if (!writer_->AttachStreamClient(connection_id, client_id)) {
            (void)flv_source->DetachFlvClient(client_id);
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
    HttpAccess *access_ = nullptr;
    HttpStreamWriter *writer_ = nullptr;
    IMediaService *media_service_ = nullptr;
    IStreamBrowserSource *stream_browser_source_ = nullptr;
    IStreamFlvSource *stream_flv_source_ = nullptr;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpAccess *access, HttpStreamWriter *writer, IMediaService *media_service,
    IStreamBrowserSource *stream_browser_source,
    IStreamFlvSource *stream_flv_source) {
    return std::unique_ptr<IStreamingHttpHandler>(
        new StreamingHttpHandler(access, writer, media_service,
                                 stream_browser_source, stream_flv_source));
}

}  // namespace live_stream
