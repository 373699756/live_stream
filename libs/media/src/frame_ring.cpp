#include "frame_ring.h"

#include <utility>

namespace live_stream {
namespace media_internal {

FrameRing::~FrameRing() { Clear(); }

FrameSubscriptionId FrameRing::AttachReader(
    const FrameSubscriptionOptions &options, size_t max_readers) {
    if (readers_.size() >= max_readers) {
        return 0;
    }
    const StreamCache *cache =
        FindCache(options.stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return 0;
    }
    const FrameSubscriptionId reader_id = next_reader_id_++;
    ReaderState reader;
    reader.stream_id = options.stream_id;
    reader.keyframe_first = options.keyframe_first;
    // 如果当前 GOP cache 尚不完整，keyframe-first reader 只能等待后续关键帧，
    // 不能从中间 P 帧开始推送给协议客户端。
    reader.waiting_for_keyframe = options.keyframe_first && !cache->complete;
    reader.reader_name = options.subscriber_name;
    reader.start_sequence = next_sequence_;
    reader.start_generation = cache->generation;
    reader.next_sequence = next_sequence_;
    reader.generation = cache->generation;
    readers_[reader_id] = std::move(reader);
    return reader_id;
}

bool FrameRing::DetachReader(FrameSubscriptionId reader_id,
                             FrameSubscriptionCloseReason reason) {
    auto iter = readers_.find(reader_id);
    if (iter == readers_.end()) {
        return false;
    }
    iter->second.close_reason = reason;
    ClearLiveQueue(&iter->second.live_queue);
    readers_.erase(iter);
    return true;
}

FrameSubscriptionInfo FrameRing::GetReaderStatus(
    FrameSubscriptionId reader_id) const {
    FrameSubscriptionInfo status;
    const auto reader_iter = readers_.find(reader_id);
    if (reader_iter == readers_.end()) {
        return status;
    }
    const ReaderState &reader = reader_iter->second;
    status.attached = true;
    status.stream_id = reader.stream_id;
    status.subscription_generation = reader.generation;
    status.close_reason = reader.close_reason;
    status.waiting_for_keyframe = reader.waiting_for_keyframe;
    status.slow_subscriber = reader.live_queue.overflow;
    status.pending_frames = static_cast<uint32_t>(reader.live_queue.size);
    return status;
}

FrameSubscriptionStartData FrameRing::GetStartData(
    FrameSubscriptionId reader_id,
    const MediaStreamInfo &stream_info) const {
    FrameSubscriptionStartData start_data;
    const auto reader_iter = readers_.find(reader_id);
    if (reader_iter == readers_.end()) {
        return start_data;
    }
    const ReaderState &reader = reader_iter->second;
    const StreamCache *cache =
        FindCache(reader.stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return start_data;
    }
    start_data.track_ready = stream_info.track_ready;
    start_data.gop_complete =
        cache->complete && reader.start_generation == cache->generation;
    start_data.subscription_generation = reader.start_generation;
    start_data.stream_info = stream_info;
    if (!start_data.gop_complete) {
        return start_data;
    }
    // start data 只返回 reader 创建之前已经存在的 GOP；创建后的帧走
    // live queue，避免同一帧在起始数据和 live 数据中重复。
    start_data.gop_frames.reserve(cache->size);
    for (size_t i = 0; i < cache->size; ++i) {
        if (cache->frames[i].sequence >= reader.start_sequence) {
            continue;
        }
        EncodedFrame frame;
        // CopyFrameForSubscription 会 ref copy payload.encoded_frame；返回给 reader 的
        // start data 与内部 GOP cache 共享 FrameBuffer，不深拷贝帧内容。
        if (CopyFrameForSubscription(cache->frames[i].payload,
                                     cache->frames[i].duration_us, &frame)) {
            start_data.gop_frames.push_back(frame);
        }
    }
    return start_data;
}

bool FrameRing::PopFrame(FrameSubscriptionId reader_id,
                         SubscribedFrame *frame) {
    if (frame == nullptr) {
        return false;
    }
    SubscribedFrameUnref(frame);
    auto reader_iter = readers_.find(reader_id);
    if (reader_iter == readers_.end()) {
        return false;
    }
    ReaderState &reader = reader_iter->second;
    QueuedFrame queued_frame;
    if (!PopLiveQueue(&reader.live_queue, &queued_frame)) {
        return false;
    }
    frame->subscription_id = reader_id;
    frame->subscription_generation = reader.generation;
    frame->starts_on_keyframe = queued_frame.starts_on_keyframe;
    const bool copied =
        CopyFrameForSubscription(queued_frame.payload,
                                 queued_frame.duration_us, &frame->frame);
    FramePayloadUnref(&queued_frame.payload);
    if (!copied) {
        SubscribedFrameUnref(frame);
        return false;
    }
    return true;
}

void FrameRing::Clear() {
    for (auto &item : readers_) {
        ClearLiveQueue(&item.second.live_queue);
        item.second.close_reason = FrameSubscriptionCloseReason::kStreamStopped;
    }
    readers_.clear();
    ClearCache(&main_cache_);
    ClearCache(&sub_cache_);
    next_reader_id_ = 1;
    next_sequence_ = 1;
}

void FrameRing::ClearStream(StreamId stream_id,
                            FrameSubscriptionCloseReason reason) {
    StreamCache *cache = FindCache(stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return;
    }
    ClearCache(cache);
    ++cache->generation;
    // stream stop、codec 切换或 timestamp reset 后，所有 reader 都必须从
    // 新一代缓存的关键帧重新开始。
    for (auto &item : readers_) {
        ReaderState &reader = item.second;
        if (reader.stream_id == stream_id) {
            ResetReaderForStream(&reader, *cache, reason);
        }
    }
}

size_t FrameRing::ReaderCount() const { return readers_.size(); }

uint32_t FrameRing::SlowReaderCount() const {
    uint32_t count = 0;
    for (const auto &item : readers_) {
        if (item.second.live_queue.overflow) {
            ++count;
        }
    }
    return count;
}

uint32_t FrameRing::SlowReaderCount(StreamId stream_id) const {
    uint32_t count = 0;
    for (const auto &item : readers_) {
        if (item.second.stream_id == stream_id &&
            item.second.live_queue.overflow) {
            ++count;
        }
    }
    return count;
}

uint32_t FrameRing::CachedFrameCount() const {
    return static_cast<uint32_t>(main_cache_.size + sub_cache_.size);
}

uint32_t FrameRing::CachedBytes() const {
    return main_cache_.bytes + sub_cache_.bytes;
}

int64_t FrameRing::LastFrameTimestamp(StreamId stream_id) const {
    const StreamCache *cache =
        FindCache(stream_id, &main_cache_, &sub_cache_);
    return cache != nullptr ? cache->last_frame_timestamp_us : 0;
}

void FrameRing::Write(const FramePayload &frame) {
    const EncodedFrame &encoded_frame = frame.encoded_frame;
    if (!EncodedFrameHasPayload(&encoded_frame)) {
        return;
    }

    StreamCache *cache =
        FindCache(encoded_frame.stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return;
    }
    const bool key_frame = encoded_frame.frame_type == FrameType::kIdr ||
                           encoded_frame.frame_type == FrameType::kI;
    const int64_t duration_us = EstimateFrameDuration(*cache, frame);
    const uint64_t sequence = next_sequence_++;
    (void)AppendToCache(cache, sequence, key_frame, duration_us, frame);
    cache->last_dts_us = encoded_frame.dts_us;
    cache->last_frame_timestamp_us = encoded_frame.dts_us;

    for (auto &item : readers_) {
        ReaderState &reader = item.second;
        if (reader.stream_id != encoded_frame.stream_id) {
            continue;
        }
        if (reader.waiting_for_keyframe && !key_frame) {
            continue;
        }
        const bool starts_on_keyframe =
            reader.waiting_for_keyframe && key_frame;
        reader.waiting_for_keyframe = false;
        reader.next_sequence = sequence + 1;
        reader.generation = cache->generation;

        if (!PushLiveQueue(&reader.live_queue, sequence, key_frame,
                           starts_on_keyframe, duration_us, frame)) {
            // 慢 reader 队列溢出后丢弃旧帧并等待下一个关键帧，避免从 P 帧
            // 继续输出导致下游解码花屏或卡住。
            reader.waiting_for_keyframe = true;
            reader.next_sequence = next_sequence_;
            reader.close_reason = FrameSubscriptionCloseReason::kCacheOverflow;
        } else {
            reader.close_reason = FrameSubscriptionCloseReason::kNone;
        }
    }
}

FrameRing::StreamCache *FrameRing::FindCache(StreamId stream_id,
                                             StreamCache *main_cache,
                                             StreamCache *sub_cache) {
    if (stream_id == StreamId::kMain) {
        return main_cache;
    }
    if (stream_id == StreamId::kSub) {
        return sub_cache;
    }
    return nullptr;
}

const FrameRing::StreamCache *FrameRing::FindCache(
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

void FrameRing::ClearCache(StreamCache *cache) {
    if (cache == nullptr) {
        return;
    }
    for (CachedFrame &frame : cache->frames) {
        FramePayloadUnref(&frame.payload);
        frame.sequence = 0;
        frame.key_frame = false;
        frame.duration_us = 0;
        frame.bytes = 0;
    }
    cache->size = 0;
    cache->bytes = 0;
    cache->complete = false;
    cache->last_frame_timestamp_us = 0;
    cache->last_dts_us = -1;
}

void FrameRing::ClearLiveQueue(LiveQueue *queue) {
    if (queue == nullptr) {
        return;
    }
    for (QueuedFrame &frame : queue->frames) {
        FramePayloadUnref(&frame.payload);
        frame.sequence = 0;
        frame.key_frame = false;
        frame.starts_on_keyframe = false;
        frame.duration_us = 0;
    }
    queue->head = 0;
    queue->size = 0;
    queue->overflow = false;
}

bool FrameRing::PushLiveQueue(LiveQueue *queue, uint64_t sequence,
                              bool key_frame, bool starts_on_keyframe,
                              int64_t duration_us,
                              const FramePayload &frame) {
    if (queue == nullptr) {
        return false;
    }
    if (queue->size >= queue->frames.size()) {
        ClearLiveQueue(queue);
        queue->overflow = true;
        return false;
    }
    const size_t index = (queue->head + queue->size) % queue->frames.size();
    QueuedFrame &queued_frame = queue->frames[index];
    FramePayloadUnref(&queued_frame.payload);
    if (!FramePayloadRefCopy(&queued_frame.payload, &frame)) {
        ClearLiveQueue(queue);
        queue->overflow = true;
        return false;
    }
    queued_frame.sequence = sequence;
    queued_frame.key_frame = key_frame;
    queued_frame.starts_on_keyframe = starts_on_keyframe;
    queued_frame.duration_us = duration_us;
    ++queue->size;
    return true;
}

bool FrameRing::PopLiveQueue(LiveQueue *queue, QueuedFrame *frame) {
    if (queue == nullptr || frame == nullptr || queue->size == 0) {
        return false;
    }
    QueuedFrame &source = queue->frames[queue->head];
    frame->sequence = source.sequence;
    frame->key_frame = source.key_frame;
    frame->starts_on_keyframe = source.starts_on_keyframe;
    frame->duration_us = source.duration_us;
    if (!FramePayloadMove(&frame->payload, &source.payload)) {
        return false;
    }
    source.sequence = 0;
    source.key_frame = false;
    source.starts_on_keyframe = false;
    source.duration_us = 0;
    queue->head = (queue->head + 1) % queue->frames.size();
    --queue->size;
    if (queue->size == 0) {
        queue->head = 0;
        queue->overflow = false;
    }
    return true;
}

bool FrameRing::CopyFrameForSubscription(const FramePayload &payload,
                                         int64_t duration_us,
                                         EncodedFrame *frame) {
    if (!EncodedFrameRefCopy(frame, &payload.encoded_frame)) {
        return false;
    }
    frame->duration_us = duration_us;
    return true;
}

uint32_t FrameRing::CachedFrameBytes(const CachedFrame &frame) {
    return frame.bytes;
}

int64_t FrameRing::EstimateFrameDuration(const StreamCache &cache,
                                         const FramePayload &frame) {
    const int64_t dts_us = frame.encoded_frame.dts_us;
    int64_t duration_us = 0;
    if (cache.last_dts_us >= 0 && dts_us > cache.last_dts_us) {
        duration_us = dts_us - cache.last_dts_us;
    }
    return duration_us;
}

bool FrameRing::AppendToCache(StreamCache *cache, uint64_t sequence,
                              bool key_frame, int64_t duration_us,
                              const FramePayload &frame) {
    if (cache == nullptr) {
        return false;
    }
    if (key_frame) {
        // GOP cache 以关键帧为边界重建；关键帧之前的帧不能作为新客户端
        // 起播点。
        ClearCache(cache);
        cache->complete = true;
        ++cache->generation;
    }
    if (!cache->complete) {
        // 还没见到关键帧前不缓存 P 帧，但 live reader 若不要求 keyframe-first
        // 仍可按上层策略接收 live queue。
        return true;
    }
    if (cache->size >= cache->frames.size()) {
        ClearCache(cache);
        ++cache->generation;
        return false;
    }
    CachedFrame &cached_frame = cache->frames[cache->size];
    cache->bytes -= CachedFrameBytes(cached_frame);
    FramePayloadUnref(&cached_frame.payload);
    // GOP cache 只增加 FramePayload/FrameBuffer 引用，缓存的是编码帧 owner；
    // 不按 GOP 再复制一份大 payload。
    if (!FramePayloadRefCopy(&cached_frame.payload, &frame)) {
        ClearCache(cache);
        ++cache->generation;
        return false;
    }
    cached_frame.sequence = sequence;
    cached_frame.key_frame = key_frame;
    cached_frame.duration_us = duration_us;
    cached_frame.bytes = frame.encoded_frame.payload.size;
    cache->bytes += cached_frame.bytes;
    ++cache->size;
    return true;
}

void FrameRing::ResetReaderForStream(ReaderState *reader,
                                     const StreamCache &cache,
                                     FrameSubscriptionCloseReason reason) {
    if (reader == nullptr) {
        return;
    }
    reader->waiting_for_keyframe = true;
    reader->next_sequence = next_sequence_;
    reader->generation = cache.generation;
    reader->close_reason = reason;
    ClearLiveQueue(&reader->live_queue);
}

}  // namespace media_internal
}  // namespace live_stream
