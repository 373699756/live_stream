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
    // FLV tag header 中 timestamp 分散在 4/5/6/7 字节。只复制 header 并重写
    // timestamp，tag body 仍直接引用原输入，减少热路径复制。
    uint8_t header[kFlvTagHeaderSize];
    std::memcpy(header, data, sizeof(header));
    WriteFlvTimestampMs(timestamp_ms, header);

    MediaSlice slices[2];
    // data 指向 start/sequence 这类 session 内字符串或栈上 header，未带 owner；
    // HTTP/net 入队时会复制这些小块。
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

    // 只替换 FLV tag header 中的时间戳；媒体 payload 分片仍指向原 FrameBuffer，
    // 由 MediaSlice.owner 保证异步发送期间 buffer 存活。
    size_t index = 0;
    while (index < tag.slice_count) {
        MediaSlice slices[kMaxNetBufferSlices];
        size_t slice_count = 0;
        while (index < tag.slice_count && slice_count < kMaxNetBufferSlices) {
            const MediaFlvVideoTagSlice &source = tag.slices[index];
            if (source.data == nullptr || source.size == 0) {
                return false;
            }
            if (source.media_payload && frame.payload.buffer == nullptr) {
                return false;
            }
            slices[slice_count].data =
                index == kFlvVideoTagHeaderSliceIndex ? rebased_header
                                                      : source.data;
            slices[slice_count].size = source.size;
            if (source.media_payload) {
                slices[slice_count].owner = frame.payload.buffer;
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

    // cached GOP 的 header 和媒体 payload 可能已经拆成不同内存块；这里保持原分片
    // 输出，只在第一个 header slice 上替换 rebased timestamp。
    size_t index = 0;
    while (index < tag.slice_count) {
        MediaSlice slices[kMaxNetBufferSlices];
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
                if (tag.frame.payload.buffer == nullptr) {
                    return false;
                }
                // cached GOP 的媒体 payload 仍在 tag.frame.payload.buffer 中；owner 让
                // net send queue 在异步写 socket 期间持有该 FrameBuffer。
                slices[slice_count].owner = tag.frame.payload.buffer;
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
    // kNoSession 表示 HTTP session 已不存在，调用方无需再 CloseConnection；
    // 其他启动中失败都可能已经发出部分 FLV 响应，需要主动关闭连接。
    return status != HttpFlvSessionStartStatus::kStarted &&
           status != HttpFlvSessionStartStatus::kNoSession;
}

HttpFlvSession::HttpFlvSession(HttpMediaWriter *writer,
                               ConnectionId connection_id,
                               StreamId stream_id)
    : writer_(writer), connection_id_(connection_id), stream_id_(stream_id) {}

HttpFlvSessionStartStatus HttpFlvSession::Start(
    const MediaFlvStart &flv_start, size_t *cached_flv_bytes) {
    if (cached_flv_bytes != nullptr) {
        *cached_flv_bytes = 0;
    }
    if (writer_ == nullptr ||
        !writer_->BeginStream(connection_id_, HttpMediaClientType::kFlv,
                              stream_id_)) {
        return HttpFlvSessionStartStatus::kNoSession;
    }

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "video/x-flv";
    headers["Cache-Control"] = "no-cache";
    headers["Pragma"] = "no-cache";
    const std::string header_block = BuildHttpStreamHeaderBlock(200, headers);

    start_block_.clear();
    start_block_.reserve(header_block.size() + flv_start.file_header.size());
    start_block_.append(header_block);
    start_block_.append(flv_start.file_header);
    if (!writer_->EnqueueStreamingChunk(
            connection_id_,
            reinterpret_cast<const uint8_t *>(start_block_.data()),
            start_block_.size())) {
        return HttpFlvSessionStartStatus::kStartBlock;
    }

    // sequence header 先于缓存 GOP 输出，保证浏览器解码器拿到 SPS/PPS/VPS 后
    // 再处理后续视频 tag。
    sequence_header_ = flv_start.sequence_header;
    if (!OnFlvChunk(reinterpret_cast<const uint8_t *>(sequence_header_.data()),
                    sequence_header_.size())) {
        return HttpFlvSessionStartStatus::kSequenceHeader;
    }

    size_t bytes = 0;
    for (const MediaFlvCachedVideoTag &cached_tag :
         flv_start.cached_video_tags) {
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
         MediaStreamIdToJson(stream_id_),
         header_block.size(), flv_start.file_header.size(),
         flv_start.sequence_header.size(),
         flv_start.cached_video_tags.size(), bytes,
         flv_start.cached_gop_complete ? 1 : 0);
    return HttpFlvSessionStartStatus::kStarted;
}

bool HttpFlvSession::OnFlvChunk(const uint8_t *data, size_t size) {
    if (writer_ == nullptr || data == nullptr || size == 0) {
        return writer_ != nullptr;
    }

    uint32_t body_size = 0;
    if (!IsCompleteFlvVideoTag(data, size, &body_size)) {
        // file header、metadata 或非完整视频 tag 不做时间戳重写，直接按原始块发送。
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

    // live video tag 的第一个 slice 必须是 FLV header，后续 slice 可以引用媒体帧。
    // header 放栈上即可，因为 net 对无 owner slice 会复制。
    uint8_t header[kMaxMediaFlvHeaderSliceBytes] = {};
    if (tag.slices[0].data == nullptr ||
        tag.slices[0].size > sizeof(header)) {
        return false;
    }
    std::memcpy(header, tag.slices[0].data, tag.slices[0].size);
    WriteFlvTimestampMs(rebased_ms, header);

    // header 是本函数栈内小数组，net 会复制；后续 media_payload slice
    // 通过 frame.payload.buffer owner 零拷贝排入发送队列。
    return EnqueueFlvVideoTagSlices(writer_, connection_id_, tag, frame,
                                    header);
}

bool HttpFlvSession::OnCachedFlvVideoTag(
    const MediaFlvCachedVideoTag &tag) {
    if (writer_ == nullptr || tag.slice_count == 0) {
        return writer_ != nullptr;
    }
    const uint32_t rebased_ms = RebaseTimestamp(tag.timestamp_ms, true);

    // cached tag 的第一个 slice 必须来自缓存 header，不能是 media_payload；
    // 否则无法安全重写 timestamp。
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
    // 每个 HTTP-FLV 连接从 0 开始计时；回放缓存 GOP 再切 live frame 时，
    // clamp_backward 防止时间戳回跳造成浏览器播放卡顿。
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
