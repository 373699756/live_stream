#include "frame_clients.h"

#include <algorithm>
#include <utility>

namespace live_stream {
namespace media_internal {

FrameClients::~FrameClients() { Clear(); }

void FrameClients::Configure(const FrameClientsOptions &options) {
    options_ = options;
    main_shared_frames_.Configure(options_.max_shared_frames,
                                  options_.max_shared_bytes);
    sub_shared_frames_.Configure(options_.max_shared_frames,
                                 options_.max_shared_bytes);
}

void FrameClients::ResetStats() {
    main_cache_drops_ = 0;
    sub_cache_drops_ = 0;
    main_client_drops_ = 0;
    sub_client_drops_ = 0;
}

FrameSubscriptionId FrameClients::SubscribeFrames(
    const SubscriptionOptions &options, size_t max_subscriptions) {
    if (clients_.size() >= max_subscriptions) {
        return 0;
    }
    const StreamCache &cache = CacheFor(options.stream_id);
    const SharedFrames &frames = SharedFramesFor(options.stream_id);

    const FrameSubscriptionId subscription_id = next_subscription_id_++;
    ClientState client;
    client.stream_id = options.stream_id;
    client.keyframe_first = options.keyframe_first;
    client.wait_keyframe = options.keyframe_first && !cache.complete;
    client.start_sequence = frames.NextSequence();
    client.start_gop_version = cache.gop_version;
    client.frame_position.next_sequence = frames.NextSequence();
    client.frame_position.stream_reset_version = cache.stream_reset_version;
    client.slow = false;
    clients_[subscription_id] = std::move(client);
    return subscription_id;
}

bool FrameClients::UnsubscribeFrames(FrameSubscriptionId subscription_id,
                                     SubscriptionClose reason) {
    auto iter = clients_.find(subscription_id);
    if (iter == clients_.end()) {
        return false;
    }
    iter->second.close_reason = reason;
    clients_.erase(iter);
    return true;
}

SubscriptionInfo FrameClients::GetSubscriptionInfo(
    FrameSubscriptionId subscription_id) const {
    SubscriptionInfo info;
    const auto client_iter = clients_.find(subscription_id);
    if (client_iter == clients_.end()) {
        return info;
    }
    const ClientState &client = client_iter->second;
    const SharedFrames &frames = SharedFramesFor(client.stream_id);
    info.open = true;
    info.stream_id = client.stream_id;
    info.generation = client.frame_position.stream_reset_version;
    info.close_reason = client.close_reason;
    info.wait_keyframe = client.wait_keyframe;
    info.slow = client.slow;
    info.pending_frames = PendingFrameSize(client, frames);
    return info;
}

SubscriptionStart FrameClients::GetSubscriptionStart(
    FrameSubscriptionId subscription_id,
    const MediaStreamInfo &stream_info) const {
    SubscriptionStart start_data;
    const auto client_iter = clients_.find(subscription_id);
    if (client_iter == clients_.end()) {
        return start_data;
    }
    const ClientState &client = client_iter->second;
    const StreamCache &cache = CacheFor(client.stream_id);
    start_data.track_ready = stream_info.track_ready;
    start_data.gop_complete =
        cache.complete && client.start_gop_version == cache.gop_version;
    start_data.generation = client.frame_position.stream_reset_version;
    start_data.stream_info = stream_info;
    if (!start_data.gop_complete) {
        return start_data;
    }

    start_data.gop_frames.reserve(cache.size);
    for (size_t i = 0; i < cache.size; ++i) {
        if (cache.frames[i].sequence >= client.start_sequence) {
            continue;
        }
        MediaFrame frame;
        CopyFrameForSubscription(cache.frames[i].payload,
                                 cache.frames[i].duration_us, frame);
        start_data.gop_frames.push_back(frame);
    }
    return start_data;
}

bool FrameClients::PullFrame(FrameSubscriptionId subscription_id,
                             SubscriptionFrame *frame) {
    if (frame == nullptr) {
        return false;
    }
    *frame = SubscriptionFrame{};
    auto client_iter = clients_.find(subscription_id);
    if (client_iter == clients_.end()) {
        return false;
    }
    ClientState &client = client_iter->second;
    const SharedFrames &frames = SharedFramesFor(client.stream_id);

    CachedFrame cached_frame;
    if (!PullSharedFrame(client, frames, cached_frame)) {
        return false;
    }
    frame->subscription_id = subscription_id;
    frame->generation = client.frame_position.stream_reset_version;
    frame->starts_on_keyframe = cached_frame.starts_on_keyframe;
    CopyFrameForSubscription(cached_frame.payload,
                             cached_frame.duration_us, frame->frame);
    return true;
}

void FrameClients::Clear() {
    for (auto &item : clients_) {
        item.second.close_reason = SubscriptionClose::kStreamStopped;
    }
    clients_.clear();
    ClearCache(main_cache_);
    ClearCache(sub_cache_);
    main_shared_frames_.Clear();
    sub_shared_frames_.Clear();
    ResetStats();
    next_subscription_id_ = 1;
}

void FrameClients::ClearStream(StreamId stream_id,
                               SubscriptionClose reason) {
    StreamCache &cache = CacheFor(stream_id);
    SharedFrames &frames = SharedFramesFor(stream_id);
    ClearCache(cache);
    ++cache.stream_reset_version;
    frames.Clear();
    for (auto &item : clients_) {
        ClientState &client = item.second;
        if (client.stream_id == stream_id) {
            ResetClientForStream(client, cache, frames, reason);
        }
    }
}

size_t FrameClients::ClientSize() const { return clients_.size(); }

uint32_t FrameClients::SlowClientSize() const {
    uint32_t slow_size = 0;
    for (const auto &item : clients_) {
        if (item.second.slow) {
            ++slow_size;
        }
    }
    return slow_size;
}

uint32_t FrameClients::SlowClientSize(StreamId stream_id) const {
    uint32_t slow_size = 0;
    for (const auto &item : clients_) {
        if (item.second.stream_id == stream_id && item.second.slow) {
            ++slow_size;
        }
    }
    return slow_size;
}

uint32_t FrameClients::CachedFrameSize() const {
    return static_cast<uint32_t>(main_cache_.size + sub_cache_.size +
                                 main_shared_frames_.Size() +
                                 sub_shared_frames_.Size());
}

uint32_t FrameClients::CachedBytes() const {
    return main_cache_.bytes + sub_cache_.bytes + main_shared_frames_.Bytes() +
           sub_shared_frames_.Bytes();
}

uint32_t FrameClients::CachedBytes(StreamId stream_id) const {
    const StreamCache *cache =
        FindCache(stream_id, &main_cache_, &sub_cache_);
    const SharedFrames *frames =
        FindSharedFrames(stream_id, &main_shared_frames_,
                         &sub_shared_frames_);
    if (cache == nullptr || frames == nullptr) {
        return 0;
    }
    return cache->bytes + frames->Bytes();
}

uint32_t FrameClients::SharedBytes(StreamId stream_id) const {
    const SharedFrames *frames =
        FindSharedFrames(stream_id, &main_shared_frames_,
                         &sub_shared_frames_);
    return frames == nullptr ? 0 : frames->Bytes();
}

int64_t FrameClients::LastFrameTimestamp(StreamId stream_id) const {
    const StreamCache *cache =
        FindCache(stream_id, &main_cache_, &sub_cache_);
    return cache == nullptr ? 0 : cache->last_frame_timestamp_us;
}

uint64_t FrameClients::CacheDropSize(StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
        return main_cache_drops_;
    }
    if (stream_id == StreamId::kSub) {
        return sub_cache_drops_;
    }
    return 0;
}

uint64_t FrameClients::ClientDropSize(StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
        return main_client_drops_;
    }
    if (stream_id == StreamId::kSub) {
        return sub_client_drops_;
    }
    return 0;
}

void FrameClients::Write(const FramePayload &frame) {
    const MediaFrame &encoded_frame = frame.frame;
    if (!IsMediaFramePayloadValid(encoded_frame)) {
        return;
    }

    StreamCache &cache = CacheFor(encoded_frame.stream_id);
    SharedFrames &frames = SharedFramesFor(encoded_frame.stream_id);

    const bool keyframe = encoded_frame.frame_type == FrameType::kIdr ||
                          encoded_frame.frame_type == FrameType::kI;
    const int64_t duration_us = EstimateFrameDuration(cache, frame);
    uint64_t sequence = 0;
    (void)PushSharedFrame(frames, keyframe, duration_us, sequence, frame);
    (void)AppendToCache(cache, sequence, keyframe, duration_us, frame);
    cache.last_dts_us = encoded_frame.dts_us;
    cache.last_frame_timestamp_us = encoded_frame.dts_us;
}

const FrameClients::StreamCache *FrameClients::FindCache(
    StreamId stream_id, const StreamCache *main_cache,
    const StreamCache *sub_cache) {
    if (stream_id == StreamId::kMain) {
        return main_cache;
    }
    if (stream_id == StreamId::kSub) {
        return sub_cache;
    }
    return nullptr;
}

const FrameClients::SharedFrames *FrameClients::FindSharedFrames(
    StreamId stream_id, const SharedFrames *main_frames,
    const SharedFrames *sub_frames) {
    if (stream_id == StreamId::kMain) {
        return main_frames;
    }
    if (stream_id == StreamId::kSub) {
        return sub_frames;
    }
    return nullptr;
}

FrameClients::StreamCache &FrameClients::CacheFor(StreamId stream_id) {
    return stream_id == StreamId::kMain ? main_cache_ : sub_cache_;
}

FrameClients::SharedFrames &FrameClients::SharedFramesFor(
    StreamId stream_id) {
    return stream_id == StreamId::kMain ? main_shared_frames_
                                        : sub_shared_frames_;
}

const FrameClients::StreamCache &FrameClients::CacheFor(
    StreamId stream_id) const {
    return stream_id == StreamId::kMain ? main_cache_ : sub_cache_;
}

const FrameClients::SharedFrames &FrameClients::SharedFramesFor(
    StreamId stream_id) const {
    return stream_id == StreamId::kMain ? main_shared_frames_
                                        : sub_shared_frames_;
}

uint32_t FrameClients::FrameBytes(const FramePayload &frame) {
    return frame.frame.payload.Size();
}

uint32_t FrameClients::CachedFrameBytes(const CachedFrame &frame) {
    return frame.bytes;
}

int64_t FrameClients::EstimateFrameDuration(const StreamCache &cache,
                                            const FramePayload &frame) {
    const int64_t dts_us = frame.frame.dts_us;
    int64_t duration_us = 0;
    if (cache.last_dts_us >= 0 && dts_us > cache.last_dts_us) {
        duration_us = dts_us - cache.last_dts_us;
    }
    return duration_us;
}

void FrameClients::CopyFrameForSubscription(const FramePayload &payload,
                                            int64_t duration_us,
                                            MediaFrame &frame) {
    frame = payload.frame;
    frame.duration_us = duration_us;
}

void FrameClients::ClearCache(StreamCache &cache) {
    for (CachedFrame &frame : cache.frames) {
        frame = CachedFrame{};
    }
    cache.size = 0;
    cache.bytes = 0;
    cache.complete = false;
    cache.last_frame_timestamp_us = 0;
    cache.last_dts_us = -1;
}

bool FrameClients::AppendToCache(StreamCache &cache, uint64_t sequence,
                                 bool keyframe, int64_t duration_us,
                                 const FramePayload &frame) {
    if (keyframe) {
        ClearCache(cache);
        cache.complete = true;
        ++cache.gop_version;
    }
    if (!cache.complete) {
        return true;
    }

    const size_t max_gop_frames =
        std::min<size_t>(options_.max_gop_frames, cache.frames.size());
    const uint32_t frame_bytes = FrameBytes(frame);
    if (max_gop_frames == 0 || cache.size >= max_gop_frames ||
        frame_bytes > options_.max_gop_bytes ||
        cache.bytes > options_.max_gop_bytes - frame_bytes) {
        DropCache(cache);
        return false;
    }

    CachedFrame &cached_frame = cache.frames[cache.size];
    cache.bytes -= CachedFrameBytes(cached_frame);
    cached_frame.payload = frame;
    cached_frame.sequence = sequence;
    cached_frame.keyframe = keyframe;
    cached_frame.starts_on_keyframe = false;
    cached_frame.duration_us = duration_us;
    cached_frame.bytes = frame_bytes;
    cache.bytes += cached_frame.bytes;
    ++cache.size;
    return true;
}

void FrameClients::DropCache(StreamCache &cache) {
    if (&cache == &main_cache_) {
        ++main_cache_drops_;
    } else if (&cache == &sub_cache_) {
        ++sub_cache_drops_;
    }
    ClearCache(cache);
    ++cache.gop_version;
}

bool FrameClients::PushSharedFrame(SharedFrames &frames, bool keyframe,
                                   int64_t duration_us, uint64_t &sequence,
                                   const FramePayload &frame) {
    CachedFrame cached_frame;
    cached_frame.payload = frame;
    cached_frame.sequence = frames.NextSequence();
    cached_frame.keyframe = keyframe;
    cached_frame.starts_on_keyframe = false;
    cached_frame.duration_us = duration_us;
    cached_frame.bytes = FrameBytes(frame);
    const bool pushed = frames.Push(cached_frame, cached_frame.bytes,
                                    &sequence);
    return pushed;
}

bool FrameClients::PullSharedFrame(ClientState &client,
                                   const SharedFrames &frames,
                                   CachedFrame &frame) {
    if (client.frame_position.next_sequence < frames.FirstSequence()) {
        MarkClientSlow(client, frames);
    }
    if (frames.Size() == 0) {
        return false;
    }
    if (client.frame_position.next_sequence >= frames.NextSequence()) {
        return false;
    }

    uint64_t sequence = client.frame_position.next_sequence;
    while (sequence < frames.NextSequence()) {
        if (sequence < frames.FirstSequence()) {
            sequence = frames.FirstSequence();
        }
        CachedFrame source;
        if (!frames.Read(sequence, &source)) {
            return false;
        }
        if (client.wait_keyframe && !source.keyframe) {
            ++sequence;
            client.frame_position.next_sequence = sequence;
            continue;
        }

        frame = source;
        frame.starts_on_keyframe = client.wait_keyframe && source.keyframe;
        client.wait_keyframe = false;
        client.slow = false;
        client.close_reason = SubscriptionClose::kNone;
        client.frame_position.next_sequence = sequence + 1;
        return true;
    }
    return false;
}

uint32_t FrameClients::PendingFrameSize(
    const ClientState &client, const SharedFrames &frames) const {
    if (client.frame_position.next_sequence >= frames.NextSequence()) {
        return 0;
    }
    const uint64_t first_available =
        std::max(client.frame_position.next_sequence, frames.FirstSequence());
    const uint64_t pending = frames.NextSequence() - first_available;
    if (pending > static_cast<uint64_t>(frames.Size())) {
        return frames.Size();
    }
    return static_cast<uint32_t>(pending);
}

void FrameClients::MarkClientSlow(ClientState &client,
                                  const SharedFrames &frames) {
    if (!client.slow) {
        if (client.stream_id == StreamId::kMain) {
            ++main_client_drops_;
        } else if (client.stream_id == StreamId::kSub) {
            ++sub_client_drops_;
        }
    }
    client.slow = true;
    client.wait_keyframe = true;
    client.close_reason = SubscriptionClose::kCacheOverflow;
    client.frame_position.next_sequence = frames.FirstSequence();
}

void FrameClients::ResetClientForStream(ClientState &client,
                                        const StreamCache &cache,
                                        const SharedFrames &frames,
                                        SubscriptionClose reason) {
    client.wait_keyframe = true;
    client.slow = false;
    client.frame_position.next_sequence = frames.NextSequence();
    client.frame_position.stream_reset_version =
        cache.stream_reset_version;
    client.close_reason = reason;
}

}  // namespace media_internal
}  // namespace live_stream
