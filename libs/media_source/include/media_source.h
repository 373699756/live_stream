#ifndef LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_
#define LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_

#include "media_frame.h"

#include "media/encoded_frame.h"
#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

constexpr size_t kMaxMediaFlvVideoTagSlices = 130;
constexpr size_t kMaxMediaFlvHeaderSliceBytes = 24;
constexpr size_t kMaxMediaFlvCachedVideoTags = 128;

struct MediaFlvVideoTagSlice {
    // media_payload=false 表示 data 指向 tag 内部小 header；
    // media_payload=true 表示 data 指向 EncodedFrame payload，调用方需保持帧引用。
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool media_payload = false;
};

struct MediaFlvVideoTagView {
    MediaFlvVideoTagSlice slices[kMaxMediaFlvVideoTagSlices];
    size_t slice_count = 0;
    uint32_t timestamp_ms = 0;
};

struct MediaFlvCachedVideoTagSlice {
    // GOP cache 只复制小 header。真正的视频 payload 仍通过 frame 持有
    // FrameBuffer 引用，避免为每个新 FLV client 深拷贝整帧。
    const uint8_t *media_data = nullptr;
    uint8_t header_data[kMaxMediaFlvHeaderSliceBytes] = {};
    size_t size = 0;
    bool media_payload = false;
};

struct MediaFlvCachedVideoTag {
    EncodedFrame frame;
    MediaFlvCachedVideoTagSlice slices[kMaxMediaFlvVideoTagSlices];
    size_t slice_count = 0;
    size_t total_size = 0;
    uint32_t timestamp_ms = 0;
};

using MediaFlvClientId = uint64_t;
using MediaMjpegClientId = uint64_t;
using MediaFrameReaderId = uint64_t;

enum class MediaSourceResetReason {
    kNone = 0,
    kStreamStarted,
    kStreamStopped,
    kCodecChanged,
    kTimestampReset,
    kCacheOverflow,
};

enum class MediaFrameReaderCloseReason {
    kNone = 0,
    kDetached,
    kStreamStopped,
    kCodecChanged,
    kTimestampReset,
    kCacheOverflow,
};

inline const char *MediaSourceResetReasonName(
    MediaSourceResetReason reason) {
    switch (reason) {
        case MediaSourceResetReason::kNone:
            return "none";
        case MediaSourceResetReason::kStreamStarted:
            return "stream_started";
        case MediaSourceResetReason::kStreamStopped:
            return "stream_stopped";
        case MediaSourceResetReason::kCodecChanged:
            return "codec_changed";
        case MediaSourceResetReason::kTimestampReset:
            return "timestamp_reset";
        case MediaSourceResetReason::kCacheOverflow:
            return "cache_overflow";
    }
    return "unknown";
}

inline const char *MediaFrameReaderCloseReasonName(
    MediaFrameReaderCloseReason reason) {
    switch (reason) {
        case MediaFrameReaderCloseReason::kNone:
            return "none";
        case MediaFrameReaderCloseReason::kDetached:
            return "detached";
        case MediaFrameReaderCloseReason::kStreamStopped:
            return "stream_stopped";
        case MediaFrameReaderCloseReason::kCodecChanged:
            return "codec_changed";
        case MediaFrameReaderCloseReason::kTimestampReset:
            return "timestamp_reset";
        case MediaFrameReaderCloseReason::kCacheOverflow:
            return "cache_overflow";
    }
    return "unknown";
}

struct MediaHlsEntry {
    uint64_t sequence = 0;
    int64_t duration_us = 0;
};

struct MediaHlsPlaylist {
    bool supported = false;
    uint32_t target_duration_sec = 0;
    uint64_t media_sequence = 0;
    uint64_t first_cached_sequence = 0;
    uint64_t last_cached_sequence = 0;
    std::vector<MediaHlsEntry> entries;
};

struct MediaSegmentRef {
    // body 是带引用计数的 TS segment body。HTTP handler 发送完成后必须
    // 调用 MediaSegmentRefUnref 释放引用。
    bool found = false;
    uint64_t sequence = 0;
    int64_t duration_us = 0;
    FrameBuffer *body = nullptr;
};

struct MediaFlvStartData {
    // 新 HTTP-FLV client 先发送 file_header/sequence_header，再从
    // cached_video_tags 的关键帧 GOP 起点继续发送 live tag。
    // cached_video_tags 持有对应 EncodedFrame 引用，媒体 payload 不会因
    // GetFlvStartData 返回对象离开 media_source 锁而失效。
    bool supported = false;
    bool cached_gop_complete = false;
    uint64_t config_generation = 0;
    std::string file_header;
    std::string sequence_header;
    std::vector<MediaFlvCachedVideoTag> cached_video_tags;
};

struct MediaFrameReaderOptions {
    StreamId stream_id = StreamId::kMain;
    bool keyframe_first = true;
    std::string reader_name;
};

struct MediaFrameReaderStartData {
    // reader 创建后先读取 start data：如果 gop_complete=true，调用方可先发送
    // gop_frames，再进入 PopFrameReaderFrame 的 live queue。
    // gop_frames 里的 MediaFrame 只 ref 底层 FrameBuffer，不按 reader 深拷贝 GOP。
    bool stream_running = false;
    bool gop_complete = false;
    uint64_t reader_generation = 0;
    MediaTrack track;
    std::vector<MediaFrame> gop_frames;
};

struct MediaFrameReaderFrame {
    // starts_on_keyframe 表示该 live frame 是等待关键帧后的第一个可解码点，
    // WebRTC/RTSP 可据此刷新协议侧状态。
    // frame 持有自己的 FrameBuffer 引用，调用方发送结束后必须
    // MediaFrameReaderFrameUnref()。
    MediaFrameReaderId reader_id = 0;
    uint64_t reader_generation = 0;
    bool starts_on_keyframe = false;
    MediaFrame frame;
};

struct MediaFrameReaderStatus {
    bool attached = false;
    StreamId stream_id = StreamId::kMain;
    uint64_t reader_generation = 0;
    MediaFrameReaderCloseReason close_reason =
        MediaFrameReaderCloseReason::kNone;
    bool waiting_for_keyframe = false;
    // slow_reader 表示该 reader 的 live queue 溢出过，media_source 会丢弃
    // 旧队列并等待下一个关键帧，避免客户端从不可解码的中间帧恢复。
    bool slow_reader = false;
    uint32_t pending_frames = 0;
};

inline void MediaFlvCachedVideoTagUnref(MediaFlvCachedVideoTag *tag) {
    if (tag == nullptr) {
        return;
    }
    EncodedFrameUnref(&tag->frame);
    *tag = MediaFlvCachedVideoTag{};
}

inline bool MediaFlvCachedVideoTagRefCopy(
    MediaFlvCachedVideoTag *target,
    const MediaFlvCachedVideoTag *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    EncodedFrame retained_frame;
    if (!EncodedFrameRefCopy(&retained_frame, &source->frame)) {
        return false;
    }
    // slices 中的 media_data 指针指向 frame payload。这里先 ref copy frame，
    // 再浅拷贝 slices，确保 cached tag 的 payload 指针仍有 owner。
    MediaFlvCachedVideoTagUnref(target);
    *target = *source;
    target->frame = retained_frame;
    return true;
}

inline void MediaFlvStartDataUnref(MediaFlvStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (MediaFlvCachedVideoTag &tag : start_data->cached_video_tags) {
        MediaFlvCachedVideoTagUnref(&tag);
    }
    start_data->cached_video_tags.clear();
    start_data->file_header.clear();
    start_data->sequence_header.clear();
    start_data->supported = false;
    start_data->cached_gop_complete = false;
    start_data->config_generation = 0;
}

inline void MediaFrameReaderStartDataUnref(
    MediaFrameReaderStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (MediaFrame &frame : start_data->gop_frames) {
        MediaFrameUnref(&frame);
    }
    start_data->gop_frames.clear();
    start_data->stream_running = false;
    start_data->gop_complete = false;
    start_data->reader_generation = 0;
    start_data->track = MediaTrack{};
}

inline void MediaFrameReaderFrameUnref(MediaFrameReaderFrame *reader_frame) {
    if (reader_frame == nullptr) {
        return;
    }
    MediaFrameUnref(&reader_frame->frame);
    reader_frame->reader_id = 0;
    reader_frame->reader_generation = 0;
    reader_frame->starts_on_keyframe = false;
}

struct MediaSourceStats {
    bool enabled = false;
    uint64_t hls_segments_created = 0;
    uint32_t active_flv_clients = 0;
    uint32_t active_mjpeg_clients = 0;
    uint32_t active_frame_readers = 0;
    uint32_t cached_frames = 0;
    uint32_t cached_bytes = 0;
    uint32_t slow_reader_count = 0;
    uint32_t main_slow_reader_count = 0;
    uint32_t sub_slow_reader_count = 0;
    int64_t main_last_frame_timestamp_us = 0;
    int64_t sub_last_frame_timestamp_us = 0;
    uint64_t main_codec_generation = 0;
    uint64_t sub_codec_generation = 0;
    std::string main_last_reset_reason;
    std::string sub_last_reset_reason;
};

struct MediaSourceStatus {
    bool running = false;
    bool track_ready = false;
    bool browser_codec = false;
    bool hls_ready = false;
    bool flv_ready = false;
    bool mjpeg_ready = false;
    Codec codec = Codec::kH264;
    uint64_t codec_generation = 0;
    uint32_t hls_segment_count = 0;
    uint64_t hls_first_segment_sequence = 0;
    uint64_t hls_last_segment_sequence = 0;
    uint64_t hls_missing_segment_count = 0;
    uint64_t hls_evicted_segment_count = 0;
    uint32_t flv_sequence_header_size = 0;
    uint32_t flv_last_keyframe_size = 0;
    uint32_t hls_current_segment_size = 0;
    int64_t last_dts_us = 0;
    int64_t last_keyframe_request_ms = 0;
    int64_t last_keyframe_seen_ms = 0;
    int64_t last_first_frame_ms = 0;
    int64_t last_protocol_ready_ms = 0;
    std::string last_reset_reason;
    bool hls_supported = false;
    bool flv_supported = false;
    bool mjpeg_supported = false;
};

class IMediaFlvSink {
public:
    virtual ~IMediaFlvSink() = default;

    virtual bool OnFlvChunk(const uint8_t *data, size_t size) = 0;
    virtual bool OnFlvVideoTag(const MediaFlvVideoTagView &tag,
                               const EncodedFrame &frame) = 0;
};

class IMediaMjpegSink {
public:
    virtual ~IMediaMjpegSink() = default;

    virtual bool OnMjpegFrame(const EncodedFrame &frame) = 0;
};

class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    virtual bool IsHlsSupported(StreamId stream_id) const = 0;
    virtual bool IsFlvSupported(StreamId stream_id) const = 0;
    virtual bool IsMjpegSupported(StreamId stream_id) const = 0;
    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual Codec GetStreamCodec(StreamId stream_id) const = 0;
    virtual MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
    virtual MediaSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                             uint64_t sequence) const = 0;
    virtual MediaFlvStartData GetFlvStartData(StreamId stream_id) const = 0;
    virtual MediaSourceStatus GetBrowserStatus(StreamId stream_id) const = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameRequestType reason) = 0;
    virtual MediaSourceStats GetStats() const = 0;
};

class IMediaFlvSource {
public:
    virtual ~IMediaFlvSource() = default;

    virtual MediaFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    bool wait_for_keyframe, IMediaFlvSink *sink) = 0;
    virtual bool DetachFlvClient(MediaFlvClientId client_id) = 0;
};

class IMediaMjpegSource {
public:
    virtual ~IMediaMjpegSource() = default;

    virtual MediaMjpegClientId
    AttachMjpegClient(StreamId stream_id, IMediaMjpegSink *sink) = 0;
    virtual bool DetachMjpegClient(MediaMjpegClientId client_id) = 0;
};

class IMediaFrameSource {
public:
    virtual ~IMediaFrameSource() = default;

    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual Codec GetStreamCodec(StreamId stream_id) const = 0;
    virtual MediaFrameReaderId AttachFrameReader(
        const MediaFrameReaderOptions &options) = 0;
    virtual bool DetachFrameReader(MediaFrameReaderId reader_id,
                                   MediaFrameReaderCloseReason reason) = 0;
    virtual MediaFrameReaderStatus GetFrameReaderStatus(
        MediaFrameReaderId reader_id) const = 0;
    virtual MediaFrameReaderStartData GetFrameReaderStartData(
        MediaFrameReaderId reader_id) const = 0;
    // Pop 只从 reader live queue 取一帧；空队列返回 false，不代表 reader 关闭。
    // 返回的 frame 持有 FrameBuffer 引用，调用方用完必须 unref。
    virtual bool PopFrameReaderFrame(MediaFrameReaderId reader_id,
                                     MediaFrameReaderFrame *frame) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameRequestType reason) = 0;
};

inline MediaSegmentRef MediaSegmentRefCopy(
    const MediaSegmentRef *segment) {
    MediaSegmentRef ref;
    if (segment == nullptr || !segment->found || segment->body == nullptr) {
        return ref;
    }
    ref = *segment;
    ref.body = FrameBufferRef(segment->body);
    if (ref.body == nullptr) {
        return MediaSegmentRef{};
    }
    return ref;
}

inline void MediaSegmentRefUnref(MediaSegmentRef *segment) {
    if (segment == nullptr) {
        return;
    }
    FrameBufferUnref(segment->body);
    *segment = MediaSegmentRef{};
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_
