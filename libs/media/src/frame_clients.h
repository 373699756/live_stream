#ifndef LIVE_STREAM_MEDIA_SRC_FRAME_CLIENTS_H_
#define LIVE_STREAM_MEDIA_SRC_FRAME_CLIENTS_H_

#include "frame_payload.h"
#include "media/media_streams.h"

#include "multi_reader_queue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>

namespace live_stream {
namespace media_internal {

struct FrameClientsOptions {
    uint32_t max_gop_frames = 128;
    uint32_t max_gop_bytes = 4 * 1024 * 1024;
    uint32_t max_shared_frames = 32;
    uint32_t max_shared_bytes = 2 * 1024 * 1024;
};

class FrameClients {
public:
    FrameClients() = default;
    FrameClients(const FrameClients &) = delete;
    FrameClients &operator=(const FrameClients &) = delete;
    ~FrameClients();

    FrameSubscriptionId SubscribeFrames(
        const SubscriptionOptions &options, size_t max_subscriptions);
    void Configure(const FrameClientsOptions &options);
    bool UnsubscribeFrames(FrameSubscriptionId subscription_id,
                           SubscriptionClose reason);
    SubscriptionInfo GetSubscriptionInfo(
        FrameSubscriptionId subscription_id) const;
    SubscriptionStart GetSubscriptionStart(
        FrameSubscriptionId subscription_id,
        const MediaStreamInfo &stream_info) const;
    bool PullFrame(FrameSubscriptionId subscription_id,
                   SubscriptionFrame *frame);
    void Clear();
    void ClearStream(StreamId stream_id, SubscriptionClose reason);
    size_t ClientSize() const;
    uint32_t SlowClientSize() const;
    uint32_t SlowClientSize(StreamId stream_id) const;
    uint32_t CachedFrameSize() const;
    uint32_t CachedBytes() const;
    uint32_t CachedBytes(StreamId stream_id) const;
    uint32_t SharedBytes(StreamId stream_id) const;
    int64_t LastFrameTimestamp(StreamId stream_id) const;
    uint64_t CacheDropSize(StreamId stream_id) const;
    uint64_t ClientDropSize(StreamId stream_id) const;
    void Write(const FramePayload &frame);

private:
    static constexpr size_t kMaxCachedGopFrames = 128;
    static constexpr size_t kMaxSharedFrames = 32;

    struct CachedFrame {
        uint64_t sequence = 0;
        FramePayload payload;
        bool keyframe = false;
        bool starts_on_keyframe = false;
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
        uint64_t gop_version = 0;
        uint64_t stream_reset_version = 0;
    };

    using SharedFrames = event::MultiReaderQueue<CachedFrame,
                                                 kMaxSharedFrames>;

    struct ClientFramePosition {
        uint64_t next_sequence = 1;
        uint64_t stream_reset_version = 0;
    };

    struct ClientState {
        StreamId stream_id = StreamId::kMain;
        bool keyframe_first = true;
        bool wait_keyframe = true;
        bool slow = false;
        uint64_t start_sequence = 0;
        uint64_t start_gop_version = 0;
        ClientFramePosition frame_position;
        SubscriptionClose close_reason =
            SubscriptionClose::kNone;
    };

    static const StreamCache *FindCache(StreamId stream_id,
                                        const StreamCache *main_cache,
                                        const StreamCache *sub_cache);
    static const SharedFrames *FindSharedFrames(
        StreamId stream_id, const SharedFrames *main_frames,
        const SharedFrames *sub_frames);
    StreamCache &CacheFor(StreamId stream_id);
    SharedFrames &SharedFramesFor(StreamId stream_id);
    const StreamCache &CacheFor(StreamId stream_id) const;
    const SharedFrames &SharedFramesFor(StreamId stream_id) const;
    static uint32_t FrameBytes(const FramePayload &frame);
    static uint32_t CachedFrameBytes(const CachedFrame &frame);
    static int64_t EstimateFrameDuration(const StreamCache &cache,
                                         const FramePayload &frame);
    static void CopyFrameForSubscription(const FramePayload &payload,
                                         int64_t duration_us,
                                         MediaFrame &frame);

    void ResetStats();
    void ClearCache(StreamCache &cache);
    bool AppendToCache(StreamCache &cache, uint64_t sequence,
                       bool keyframe, int64_t duration_us,
                       const FramePayload &frame);
    void DropCache(StreamCache &cache);
    bool PushSharedFrame(SharedFrames &frames, bool keyframe,
                         int64_t duration_us, uint64_t &sequence,
                         const FramePayload &frame);
    bool PullSharedFrame(ClientState &client, const SharedFrames &frames,
                         CachedFrame &frame);
    uint32_t PendingFrameSize(const ClientState &client,
                              const SharedFrames &frames) const;
    void MarkClientSlow(ClientState &client, const SharedFrames &frames);
    void ResetClientForStream(ClientState &client,
                              const StreamCache &cache,
                              const SharedFrames &frames,
                              SubscriptionClose reason);

    FrameClientsOptions options_;
    std::map<FrameSubscriptionId, ClientState> clients_;
    FrameSubscriptionId next_subscription_id_ = 1;
    StreamCache main_cache_;
    StreamCache sub_cache_;
    SharedFrames main_shared_frames_;
    SharedFrames sub_shared_frames_;
    uint64_t main_cache_drops_ = 0;
    uint64_t sub_cache_drops_ = 0;
    uint64_t main_client_drops_ = 0;
    uint64_t sub_client_drops_ = 0;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FRAME_CLIENTS_H_
