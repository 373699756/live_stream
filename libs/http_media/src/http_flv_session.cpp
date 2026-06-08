#include "http_flv_session.h"

#include "http_media_utils.h"

#include "infra/log.h"
#include "net.h"

#include <cstdint>
#include <cstring>
#include <map>

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

bool EnqueueFlvTagWithTimestamp(HttpMediaWriter *writer,
                                ConnectionId connection_id,
                                const uint8_t *data, size_t size,
                                uint32_t timestamp_ms) {
    if (writer == nullptr || data == nullptr || size < kFlvTagHeaderSize) {
        return writer != nullptr;
    }
    uint8_t header[kFlvTagHeaderSize];
    std::memcpy(header, data, sizeof(header));
    WriteFlvTimestampMs(timestamp_ms, header);

    HttpMediaSlice slices[2];
    slices[0].data = header;
    slices[0].size = sizeof(header);
    slices[1].data = data + sizeof(header);
    slices[1].size = size - sizeof(header);
    return writer->EnqueueStreamingSlices(connection_id, slices, 2);
}

bool EnqueueFlvVideoTagSlices(HttpMediaWriter *writer,
                              ConnectionId connection_id,
                              const MediaFlvVideoTagView &tag,
                              const EncodedFrame &frame,
                              const uint8_t *rebased_header) {
    if (writer == nullptr || tag.slice_count == 0 ||
        rebased_header == nullptr) {
        return writer != nullptr;
    }

    size_t index = 0;
    while (index < tag.slice_count) {
        HttpMediaSlice slices[kMaxNetBufferSlices];
        size_t slice_count = 0;
        while (index < tag.slice_count && slice_count < kMaxNetBufferSlices) {
            const MediaFlvVideoTagSlice &source = tag.slices[index];
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

bool EnqueueCachedFlvVideoTagSlices(HttpMediaWriter *writer,
                                    ConnectionId connection_id,
                                    const MediaFlvCachedVideoTag &tag,
                                    const uint8_t *rebased_header) {
    if (writer == nullptr || tag.slice_count == 0 ||
        rebased_header == nullptr) {
        return writer != nullptr;
    }

    size_t index = 0;
    while (index < tag.slice_count) {
        HttpMediaSlice slices[kMaxNetBufferSlices];
        size_t slice_count = 0;
        while (index < tag.slice_count && slice_count < kMaxNetBufferSlices) {
            const MediaFlvCachedVideoTagSlice &source = tag.slices[index];
            if (source.size == 0) {
                return false;
            }
            slices[slice_count].data =
                index == kFlvVideoTagHeaderSliceIndex
                    ? rebased_header
                    : (source.media_payload ? source.media_data
                                            : source.header_data);
            if (slices[slice_count].data == nullptr) {
                return false;
            }
            slices[slice_count].size = source.size;
            if (source.media_payload) {
                if (tag.frame.buffer == nullptr) {
                    return false;
                }
                slices[slice_count].owner = tag.frame.buffer;
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

}  // namespace

const char *HttpFlvSessionStartStatusName(
    HttpFlvSessionStartStatus status) {
    switch (status) {
        case HttpFlvSessionStartStatus::kStarted:
            return "started";
        case HttpFlvSessionStartStatus::kNoSession:
            return "no_session";
        case HttpFlvSessionStartStatus::kStartBlock:
            return "start_block";
        case HttpFlvSessionStartStatus::kSequenceHeader:
            return "sequence_header";
        case HttpFlvSessionStartStatus::kCachedGop:
            return "cached_gop";
    }
    return "unknown";
}

bool HttpFlvSessionStartNeedsClose(
    HttpFlvSessionStartStatus status) {
    return status != HttpFlvSessionStartStatus::kStarted &&
           status != HttpFlvSessionStartStatus::kNoSession;
}

HttpFlvSession::HttpFlvSession(HttpMediaWriter *writer,
                               ConnectionId connection_id,
                               StreamId stream_id)
    : writer_(writer), connection_id_(connection_id), stream_id_(stream_id) {}

HttpFlvSessionStartStatus HttpFlvSession::Start(
    const MediaFlvStartData &start_data, size_t *cached_flv_bytes) {
    if (cached_flv_bytes != nullptr) {
        *cached_flv_bytes = 0;
    }
    if (writer_ == nullptr || !writer_->BeginStream(connection_id_)) {
        return HttpFlvSessionStartStatus::kNoSession;
    }

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "video/x-flv";
    headers["Cache-Control"] = "no-cache";
    headers["Pragma"] = "no-cache";
    const std::string header_block = BuildHttpMediaStreamingHeaderBlock(200, headers);

    start_block_.clear();
    start_block_.reserve(header_block.size() + start_data.file_header.size());
    start_block_.append(header_block);
    start_block_.append(start_data.file_header);
    if (!writer_->EnqueueStreamingChunk(
            connection_id_,
            reinterpret_cast<const uint8_t *>(start_block_.data()),
            start_block_.size())) {
        return HttpFlvSessionStartStatus::kStartBlock;
    }

    sequence_header_ = start_data.sequence_header;
    if (!OnFlvChunk(reinterpret_cast<const uint8_t *>(sequence_header_.data()),
                    sequence_header_.size())) {
        return HttpFlvSessionStartStatus::kSequenceHeader;
    }

    size_t bytes = 0;
    for (const MediaFlvCachedVideoTag &cached_tag :
         start_data.cached_video_tags) {
        if (!OnCachedFlvVideoTag(cached_tag)) {
            return HttpFlvSessionStartStatus::kCachedGop;
        }
        bytes += cached_tag.total_size;
    }
    if (cached_flv_bytes != nullptr) {
        *cached_flv_bytes = bytes;
    }
    Info(kHttpMediaModuleName,
         "HTTP-FLV start conn=%llu stream=%s header=%zu file=%zu "
         "sequence=%zu cached_flv=%zu cached_bytes=%zu "
         "gop_complete=%d",
         static_cast<unsigned long long>(connection_id_),
         HttpMediaStreamIdToJsonString(stream_id_),
         header_block.size(), start_data.file_header.size(),
         start_data.sequence_header.size(),
         start_data.cached_video_tags.size(), bytes,
         start_data.cached_gop_complete ? 1 : 0);
    return HttpFlvSessionStartStatus::kStarted;
}

bool HttpFlvSession::OnFlvChunk(const uint8_t *data, size_t size) {
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
        return EnqueueFlvTagWithTimestamp(writer_, connection_id_, data, size, 0);
    }
    const uint32_t rebased_ms = RebaseTimestamp(
        timestamp_ms, packet_type == kFlvPacketTypeCodedFrames);
    return EnqueueFlvTagWithTimestamp(writer_, connection_id_, data, size,
                                      rebased_ms);
}

bool HttpFlvSession::OnFlvVideoTag(const MediaFlvVideoTagView &tag,
                                   const EncodedFrame &frame) {
    if (writer_ == nullptr || tag.slice_count == 0) {
        return writer_ != nullptr;
    }
    const uint32_t rebased_ms = RebaseTimestamp(tag.timestamp_ms, true);

    uint8_t header[kMaxMediaFlvHeaderSliceBytes] = {};
    if (tag.slices[0].data == nullptr ||
        tag.slices[0].size > sizeof(header)) {
        return false;
    }
    std::memcpy(header, tag.slices[0].data, tag.slices[0].size);
    WriteFlvTimestampMs(rebased_ms, header);

    return EnqueueFlvVideoTagSlices(writer_, connection_id_, tag, frame,
                                    header);
}

bool HttpFlvSession::OnCachedFlvVideoTag(
    const MediaFlvCachedVideoTag &tag) {
    if (writer_ == nullptr || tag.slice_count == 0) {
        return writer_ != nullptr;
    }
    const uint32_t rebased_ms = RebaseTimestamp(tag.timestamp_ms, true);

    uint8_t header[kMaxMediaFlvHeaderSliceBytes] = {};
    if (tag.slices[0].media_payload ||
        tag.slices[0].size > sizeof(header)) {
        return false;
    }
    std::memcpy(header, tag.slices[0].header_data, tag.slices[0].size);
    WriteFlvTimestampMs(rebased_ms, header);

    return EnqueueCachedFlvVideoTagSlices(writer_, connection_id_, tag, header);
}

uint32_t HttpFlvSession::RebaseTimestamp(uint32_t timestamp_ms,
                                         bool clamp_backward) {
    if (!timestamp_base_set_) {
        timestamp_base_ms_ = timestamp_ms;
        timestamp_base_set_ = true;
        Info(kHttpMediaModuleName,
             "HTTP-FLV timestamp base conn=%llu base_ms=%u",
             static_cast<unsigned long long>(connection_id_),
             timestamp_base_ms_);
    }
    uint32_t rebased_ms =
        timestamp_ms >= timestamp_base_ms_ ? timestamp_ms - timestamp_base_ms_
                                           : last_timestamp_ms_;
    if (clamp_backward && rebased_ms < last_timestamp_ms_) {
        rebased_ms = last_timestamp_ms_;
    }
    last_timestamp_ms_ = rebased_ms;
    return rebased_ms;
}

}  // namespace live_stream
