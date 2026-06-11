#ifndef LIVE_STREAM_MEDIA_SRC_FRAME_RING_H_
#define LIVE_STREAM_MEDIA_SRC_FRAME_RING_H_

#include "frame_payload.h"
#include "media/media_streams.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>

namespace live_stream {
namespace media_internal {

class FrameRing {
public:
    FrameRing() = default;
    FrameRing(const FrameRing &) = delete;
    FrameRing &operator=(const FrameRing &) = delete;
    ~FrameRing();

    FrameSubscriptionId AttachSubscription(
        const SubscriptionOptions &options, size_t max_subscriptions);
    bool DetachSubscription(FrameSubscriptionId subscription_id,
                            SubscriptionClose reason);
    SubscriptionInfo GetSubscriptionInfo(
        FrameSubscriptionId subscription_id) const;
    SubscriptionStart GetSubscriptionStart(
        FrameSubscriptionId subscription_id,
        const MediaStreamInfo &stream_info) const;
    bool PopFrame(FrameSubscriptionId subscription_id,
                  SubscriptionFrame *frame);
    void Clear();
    void ClearStream(StreamId stream_id, SubscriptionClose reason);
    size_t SubscriptionCount() const;
    uint32_t SlowSubscriptionCount() const;
    uint32_t SlowSubscriptionCount(StreamId stream_id) const;
    uint32_t CachedFrameCount() const;
    uint32_t CachedBytes() const;
    int64_t LastFrameTimestamp(StreamId stream_id) const;
    void Write(const FramePayload &frame);

private:
    // GOP cache 从关键帧开始保存，给新 subscription 提供可独立解码的起始数据。
    static constexpr size_t kMaxCachedGopFrames = 128;
    // 每个 subscription 的 live queue 有上限；超过上限认为客户端太慢，清空队列
    // 并等待下一帧关键帧重新开始。
    static constexpr size_t kMaxQueuedLiveFrames = 32;

    struct CachedFrame {
        uint64_t sequence = 0;
        FramePayload payload;
        bool keyframe = false;
        int64_t duration_us = 0;
        uint32_t bytes = 0;
    };

    struct StreamCache {
        std::array<CachedFrame, kMaxCachedGopFrames> frames;
        size_t size = 0;
        uint32_t bytes = 0;
        bool complete = false;
        int64_t last_frame_timestamp_us = 0;
        int64_t last_dts_us = -1;
        uint64_t generation = 0;
    };

    struct QueuedFrame {
        uint64_t sequence = 0;
        FramePayload payload;
        bool keyframe = false;
        bool starts_on_keyframe = false;
        int64_t duration_us = 0;
    };

    struct LiveQueue {
        std::array<QueuedFrame, kMaxQueuedLiveFrames> frames;
        size_t head = 0;
        size_t size = 0;
        bool overflow = false;
    };

    struct SubscriptionState {
        StreamId stream_id = StreamId::kMain;
        bool keyframe_first = true;
        bool waiting_for_keyframe = true;
        uint64_t start_sequence = 0;
        uint64_t start_generation = 0;
        uint64_t next_sequence = 0;
        uint64_t generation = 0;
        LiveQueue live_queue;
        SubscriptionClose close_reason =
            SubscriptionClose::kNone;
    };

    static StreamCache *FindCache(StreamId stream_id,
                                  StreamCache *main_cache,
                                  StreamCache *sub_cache);
    static const StreamCache *FindCache(StreamId stream_id,
                                        const StreamCache *main_cache,
                                        const StreamCache *sub_cache);
    static void ClearCache(StreamCache *cache);
    static void ClearLiveQueue(LiveQueue *queue);
    static bool PushLiveQueue(LiveQueue *queue, uint64_t sequence,
                              bool keyframe, bool starts_on_keyframe,
                              int64_t duration_us,
                              const FramePayload &frame);
    static bool PopLiveQueue(LiveQueue *queue, QueuedFrame *frame);
    static bool CopyFrameForSubscription(const FramePayload &payload,
                                         int64_t duration_us,
                                         EncodedFrame *frame);
    static uint32_t CachedFrameBytes(const CachedFrame &frame);
    static int64_t EstimateFrameDuration(const StreamCache &cache,
                                         const FramePayload &frame);

    bool AppendToCache(StreamCache *cache, uint64_t sequence,
                       bool keyframe, int64_t duration_us,
                       const FramePayload &frame);
    void ResetSubscriptionForStream(SubscriptionState *subscription,
                                    const StreamCache &cache,
                                    SubscriptionClose reason);

    std::map<FrameSubscriptionId, SubscriptionState> subscriptions_;
    FrameSubscriptionId next_subscription_id_ = 1;
    uint64_t next_sequence_ = 1;
    StreamCache main_cache_;
    StreamCache sub_cache_;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FRAME_RING_H_
