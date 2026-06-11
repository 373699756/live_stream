#ifndef LIVE_STREAM_MEDIA_MEDIA_STREAMS_H_
#define LIVE_STREAM_MEDIA_MEDIA_STREAMS_H_

#include "media/encoded_frame.h"
#include "media/frame_sink.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

constexpr size_t kMaxMediaFlvVideoTagSlices = 130;
constexpr size_t kMaxMediaFlvHeaderSliceBytes = 24;
constexpr size_t kMaxMediaFlvCachedVideoTags = 128;

using RequestKeyframeFn = bool (*)(StreamId stream_id,
                                   KeyframeRequestSource source,
                                   void *user);

struct MediaStreamsOptions {
    uint32_t hls_segment_duration_ms = 2000;
    uint32_t hls_playlist_depth = 3;
    uint32_t hls_segment_retain_count = 6;
    uint32_t max_flv_clients = 8;
    uint32_t max_mjpeg_clients = 8;
    uint32_t max_frame_subscriptions = 8;
    RequestKeyframeFn request_keyframe = nullptr;
    void *request_keyframe_user = nullptr;
};

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
using FrameSubscriptionId = uint64_t;

enum class MediaStreamResetReason {
    kNone = 0,
    kStreamStarted,
    kStreamStopped,
    kCodecChanged,
    kTimestampReset,
    kCacheOverflow,
};

enum class SubscriptionClose {
    kNone = 0,
    kUnsubscribed,
    kStreamStopped,
    kCodecChanged,
    kTimestampReset,
    kCacheOverflow,
};

const char *MediaStreamResetReasonName(MediaStreamResetReason reason);
const char *SubscriptionCloseName(
    SubscriptionClose reason);

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

struct MediaFlvStart {
    // 新 HTTP-FLV client 先发送 file_header/sequence_header，再从
    // cached_video_tags 的关键帧 GOP 起点继续发送 live tag。
    // cached_video_tags 持有对应 EncodedFrame 引用，媒体 payload 不会因
    // GetFlvStart 返回对象离开 MediaStreams 锁而失效。
    bool supported = false;
    bool cached_gop_complete = false;
    uint64_t config_generation = 0;
    std::string file_header;
    std::string sequence_header;
    std::vector<MediaFlvCachedVideoTag> cached_video_tags;
};

struct SubscriptionOptions {
    StreamId stream_id = StreamId::kMain;
    bool keyframe_first = true;
};

struct MediaStreamInfo {
    bool running = false;
    bool track_ready = false;
    bool browser_codec = false;
    bool hls_ready = false;
    bool flv_ready = false;
    bool mjpeg_ready = false;
    Codec codec = Codec::kH264;
    uint32_t clock_rate = 90000;
    uint64_t codec_generation = 0;
    std::string vps;
    std::string sps;
    std::string pps;
    uint32_t hls_segment_count = 0;
    uint64_t hls_first_segment_sequence = 0;
    uint64_t hls_last_segment_sequence = 0;
    uint64_t hls_missing_segment_count = 0;
    uint64_t hls_evicted_segment_count = 0;
    uint32_t flv_sequence_header_size = 0;
    uint32_t flv_last_keyframe_size = 0;
    uint32_t hls_current_segment_size = 0;
    int64_t last_dts_us = 0;
    std::string last_reset_reason;
    bool hls_supported = false;
    bool flv_supported = false;
    bool mjpeg_supported = false;
};

struct SubscriptionStart {
    // subscription 创建后先读取 start data：如果 track_ready/gop_complete=true，
    // 调用方可先发送 gop_frames，再进入 PopSubscriptionFrame 的 live queue。
    // gop_frames 里的 EncodedFrame 只 ref 底层 FrameBuffer，不按 subscription 深拷贝 GOP。
    bool track_ready = false;
    bool gop_complete = false;
    uint64_t generation = 0;
    MediaStreamInfo stream_info;
    std::vector<EncodedFrame> gop_frames;
};

struct SubscriptionFrame {
    // starts_on_keyframe 表示该 live frame 是等待关键帧后的第一个可解码点，
    // WebRTC/RTSP 可据此刷新协议侧状态。
    // frame 持有自己的 FrameBuffer 引用，调用方发送结束后必须
    // SubscriptionFrameUnref()。
    FrameSubscriptionId subscription_id = 0;
    uint64_t generation = 0;
    bool starts_on_keyframe = false;
    EncodedFrame frame;
};

struct SubscriptionInfo {
    bool open = false;
    StreamId stream_id = StreamId::kMain;
    uint64_t generation = 0;
    SubscriptionClose close_reason =
        SubscriptionClose::kNone;
    bool waiting_for_keyframe = false;
    // slow 表示该 subscription 的 live queue 溢出过，MediaStreams 会丢弃
    // 旧队列并等待下一个关键帧，避免客户端从不可解码的中间帧恢复。
    bool slow = false;
    uint32_t pending_frames = 0;
};

struct MediaStreamStats {
    bool enabled = false;
    uint64_t hls_segments_created = 0;
    uint32_t active_flv_clients = 0;
    uint32_t active_mjpeg_clients = 0;
    uint32_t active_subscriptions = 0;
    uint32_t cached_frames = 0;
    uint32_t cached_bytes = 0;
    uint32_t slow_subscriptions = 0;
    uint32_t main_slow_subscriptions = 0;
    uint32_t sub_slow_subscriptions = 0;
    int64_t main_last_frame_timestamp_us = 0;
    int64_t sub_last_frame_timestamp_us = 0;
    uint64_t main_codec_generation = 0;
    uint64_t sub_codec_generation = 0;
    std::string main_last_reset_reason;
    std::string sub_last_reset_reason;
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

void MediaFlvCachedVideoTagUnref(MediaFlvCachedVideoTag *tag);
bool MediaFlvCachedVideoTagRefCopy(MediaFlvCachedVideoTag *target,
                                   const MediaFlvCachedVideoTag *source);
void MediaFlvStartUnref(MediaFlvStart *flv_start);
void SubscriptionStartUnref(
    SubscriptionStart *start_data);
void SubscriptionFrameUnref(SubscriptionFrame *subscribed_frame);
MediaSegmentRef MediaSegmentRefCopy(const MediaSegmentRef *segment);
void MediaSegmentRefUnref(MediaSegmentRef *segment);

class MediaStreams : public FrameSink {
public:
    explicit MediaStreams(MediaStreamsOptions options = MediaStreamsOptions());
    ~MediaStreams() override;

    bool Start();
    void Stop();
    bool PushFrame(const EncodedFrame &frame) override;
    void SetStreamState(StreamId stream_id, MediaStreamState state,
                        Codec codec);

    bool IsHlsSupported(StreamId stream_id) const;
    bool IsFlvSupported(StreamId stream_id) const;
    bool IsMjpegSupported(StreamId stream_id) const;
    bool IsStreamAvailable(StreamId stream_id) const;
    Codec GetStreamCodec(StreamId stream_id) const;
    MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const;
    MediaSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                     uint64_t sequence) const;
    MediaFlvStart GetFlvStart(StreamId stream_id) const;
    MediaStreamInfo GetStreamInfo(StreamId stream_id) const;
    MediaStreamStats GetStreamStats() const;
    bool RequestKeyframe(StreamId stream_id,
                         KeyframeRequestSource source);

    MediaFlvClientId AttachFlvClient(StreamId stream_id,
                                     uint64_t config_generation,
                                     bool wait_for_keyframe,
                                     IMediaFlvSink *sink);
    bool DetachFlvClient(MediaFlvClientId client_id);
    MediaMjpegClientId AttachMjpegClient(StreamId stream_id,
                                         IMediaMjpegSink *sink);
    bool DetachMjpegClient(MediaMjpegClientId client_id);

    FrameSubscriptionId SubscribeFrames(
        const SubscriptionOptions &options);
    bool UnsubscribeFrames(FrameSubscriptionId subscription_id,
                           SubscriptionClose reason);
    SubscriptionInfo GetSubscriptionInfo(
        FrameSubscriptionId subscription_id) const;
    SubscriptionStart GetSubscriptionStart(
        FrameSubscriptionId subscription_id) const;
    bool PopSubscriptionFrame(FrameSubscriptionId subscription_id,
                              SubscriptionFrame *frame);

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_STREAMS_H_
