#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
#include "media/encoded_frame.h"
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
constexpr size_t kFlvVideoTagHeaderSliceIndex = 0;
constexpr const char *kMjpegBoundary = "live_stream_frame";
constexpr const char *kMjpegFrameTail = "\r\n";

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

void SendStreamingError(HttpStreamWriter *writer, ConnectionId connection_id,
                        const HttpResponse &response) {
    writer->SendResponse(connection_id, response, true);
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

bool EnqueueFlvVideoTagSlices(HttpStreamWriter *writer,
                              ConnectionId connection_id,
                              const StreamFlvVideoTagView &tag,
                              const EncodedFrame &frame,
                              const uint8_t *rebased_header) {
    if (writer == nullptr || tag.slice_count == 0 ||
        rebased_header == nullptr) {
        return writer != nullptr;
    }

    size_t index = 0;
    while (index < tag.slice_count) {
        HttpStreamSlice slices[kMaxNetBufferSlices];
        size_t slice_count = 0;
        while (index < tag.slice_count && slice_count < kMaxNetBufferSlices) {
            const StreamFlvVideoTagSlice &source = tag.slices[index];
            if (source.data == nullptr || source.size == 0) {
                return false;
            }
            if (source.media_payload && frame.buffer == nullptr) {
                return false;
            }
            slices[slice_count].data =
                index == kFlvVideoTagHeaderSliceIndex ? rebased_header
                                                      : source.data;
            slices[slice_count].size = source.size;
            if (source.media_payload) {
                slices[slice_count].owner = frame.buffer;
            }
            ++slice_count;
            ++index;
        }
        if (slice_count == 0 ||
            !writer->EnqueueStreamingSlices(connection_id, slices,
                                            slice_count)) {
            return false;
        }
    }
    return true;
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

    bool OnFlvVideoTag(const StreamFlvVideoTagView &tag,
                       const EncodedFrame &frame) override {
        if (writer_ == nullptr || tag.slice_count == 0) {
            return writer_ != nullptr;
        }
        uint32_t timestamp_ms = tag.timestamp_ms;
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
        if (timestamp_base_set_ && rebased_ms < last_timestamp_ms_) {
            rebased_ms = last_timestamp_ms_;
        }
        last_timestamp_ms_ = rebased_ms;

        uint8_t header[24] = {};
        if (tag.slices[0].data == nullptr ||
            tag.slices[0].size > sizeof(header)) {
            return false;
        }
        std::memcpy(header, tag.slices[0].data, tag.slices[0].size);
        WriteFlvTimestampMs(rebased_ms, header);

        return EnqueueFlvVideoTagSlices(writer_, connection_id_, tag, frame,
                                        header);
    }

private:
    HttpStreamWriter *writer_ = nullptr;
    ConnectionId connection_id_ = 0;
    bool timestamp_base_set_ = false;
    uint32_t timestamp_base_ms_ = 0;
    uint32_t last_timestamp_ms_ = 0;
};

class MjpegConnectionSink : public IStreamMjpegSink {
public:
    MjpegConnectionSink(HttpStreamWriter *writer, ConnectionId connection_id)
        : writer_(writer), connection_id_(connection_id) {}

    bool OnMjpegFrame(const EncodedFrame &frame) override {
        if (writer_ == nullptr) {
            return false;
        }
        const uint8_t *payload = frame.PayloadData();
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

        HttpStreamSlice slices[3];
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
    HttpStreamWriter *writer_ = nullptr;
    ConnectionId connection_id_ = 0;
};

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

bool ParseMjpegStreamName(const HttpRequest &request, StreamId *stream_id,
                          std::string *stream_name) {
    if (stream_id == nullptr || stream_name == nullptr) {
        return false;
    }
    *stream_name = PathSuffix(request.path, "/api/mjpeg/");
    if (stream_name->size() <= 5 ||
        stream_name->substr(stream_name->size() - 5) != ".mjpg") {
        return false;
    }
    stream_name->resize(stream_name->size() - 5);
    return StreamIdFromJsonString(*stream_name, stream_id);
}

}  // namespace

class StreamingHttpHandler : public IStreamingHttpHandler {
public:
    StreamingHttpHandler(HttpAccess *access, HttpStreamWriter *writer,
                         IMediaService *media_service,
                         IStreamBrowserSource *stream_browser_source,
                         IStreamFlvSource *stream_flv_source,
                         IStreamMjpegSource *stream_mjpeg_source)
        : access_(access), writer_(writer), media_service_(media_service),
          stream_browser_source_(stream_browser_source),
          stream_flv_source_(stream_flv_source),
          stream_mjpeg_source_(stream_mjpeg_source) {}

    bool CanHandleStreamingRequest(const HttpRequest &request) const override {
        return request.method == HttpMethod::kGet &&
               (StartsWith(request.path, "/api/flv/") ||
                StartsWith(request.path, "/api/mjpeg/"));
    }

    void HandleStreamingRequest(ConnectionId connection_id,
                                const HttpRequest &request) override {
        if (StartsWith(request.path, "/api/mjpeg/")) {
            HandleMjpegRequest(connection_id, request);
            return;
        }
        HandleFlvRequest(connection_id, request);
    }

private:
    void HandleFlvRequest(ConnectionId connection_id,
                          const HttpRequest &request) {
        if (stream_browser_source_ == nullptr || stream_flv_source_ == nullptr) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=no_stream_hub",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               StatusResponse(501, "Not Implemented"));
            return;
        }
        if (IsMediaRestarting(media_service_)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               StatusResponse(503,
                                              "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id, auth_response);
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseFlvStreamName(request, &stream_id, &stream_name)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-FLV reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            SendStreamingError(writer_, connection_id,
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
            SendStreamingError(
                writer_, connection_id,
                StatusResponse(409, "HTTP-FLV requires H.264/H.265 stream"));
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
            SendStreamingError(writer_, connection_id,
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
            SendStreamingError(writer_, connection_id,
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

    void HandleMjpegRequest(ConnectionId connection_id,
                            const HttpRequest &request) {
        if (stream_browser_source_ == nullptr ||
            stream_mjpeg_source_ == nullptr) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=no_stream_hub",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               StatusResponse(501, "Not Implemented"));
            return;
        }
        if (IsMediaRestarting(media_service_)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=media_restarting",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id,
                               StatusResponse(503,
                                              "Media pipeline restarting"));
            return;
        }
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=auth",
                            static_cast<unsigned long long>(connection_id));
            SendStreamingError(writer_, connection_id, auth_response);
            return;
        }

        StreamId stream_id = StreamId::kMain;
        std::string stream_name;
        if (!ParseMjpegStreamName(request, &stream_id, &stream_name)) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG reject conn=%llu reason=path path=%s",
                            static_cast<unsigned long long>(connection_id),
                            request.path.c_str());
            SendStreamingError(writer_, connection_id,
                               StatusResponse(400, "Invalid MJPEG path"));
            return;
        }

        const StreamBrowserStatus browser_status =
            stream_browser_source_->GetBrowserStatus(stream_id);
        if (!browser_status.mjpeg_supported) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG reject conn=%llu stream=%s "
                            "reason=unsupported codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            SendStreamingError(
                writer_, connection_id,
                StatusResponse(409, "MJPEG preview requires MJPEG stream"));
            return;
        }
        if (!browser_status.mjpeg_ready) {
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG reject conn=%llu stream=%s "
                            "reason=not_ready codec=%s running=%d",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id),
                            VideoCodecName(browser_status.codec),
                            browser_status.running ? 1 : 0);
            SendStreamingError(writer_, connection_id,
                               StatusResponse(503, "MJPEG stream not ready"));
            return;
        }

        IStreamMjpegSink *sink =
            new MjpegConnectionSink(writer_, connection_id);
        if (writer_ == nullptr || !writer_->BeginStream(connection_id)) {
            delete sink;
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s "
                            "reason=no_session",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            return;
        }

        std::map<std::string, std::string> headers;
        headers["Content-Type"] =
            std::string("multipart/x-mixed-replace; boundary=") +
            kMjpegBoundary;
        headers["Cache-Control"] = "no-cache";
        headers["Pragma"] = "no-cache";
        const std::string header_block = BuildStreamingHeaderBlock(200, headers);
        if (!writer_->EnqueueStreamingChunk(
                connection_id,
                reinterpret_cast<const uint8_t *>(header_block.data()),
                header_block.size())) {
            delete sink;
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=enqueue",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }

        const StreamMjpegClientId client_id =
            stream_mjpeg_source_->AttachMjpegClient(stream_id, sink);
        if (client_id == 0) {
            delete sink;
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=attach",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            writer_->CloseConnection(connection_id);
            return;
        }

        if (!writer_->AttachStreamClient(connection_id, client_id)) {
            (void)stream_mjpeg_source_->DetachMjpegClient(client_id);
            INFRA_LOG_ERROR(kHttpModuleName,
                            "HTTP-MJPEG close conn=%llu stream=%s reason=closed",
                            static_cast<unsigned long long>(connection_id),
                            StreamIdToJsonString(stream_id));
            return;
        }
        INFRA_LOG_INFO(kHttpModuleName,
                       "HTTP-MJPEG attached conn=%llu stream=%s client=%llu",
                       static_cast<unsigned long long>(connection_id),
                       StreamIdToJsonString(stream_id),
                       static_cast<unsigned long long>(client_id));
    }

    HttpAccess *access_ = nullptr;
    HttpStreamWriter *writer_ = nullptr;
    IMediaService *media_service_ = nullptr;
    IStreamBrowserSource *stream_browser_source_ = nullptr;
    IStreamFlvSource *stream_flv_source_ = nullptr;
    IStreamMjpegSource *stream_mjpeg_source_ = nullptr;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpAccess *access, HttpStreamWriter *writer, IMediaService *media_service,
    IStreamBrowserSource *stream_browser_source,
    IStreamFlvSource *stream_flv_source,
    IStreamMjpegSource *stream_mjpeg_source) {
    return std::unique_ptr<IStreamingHttpHandler>(
        new StreamingHttpHandler(access, writer, media_service,
                                 stream_browser_source, stream_flv_source,
                                 stream_mjpeg_source));
}

}  // namespace live_stream
