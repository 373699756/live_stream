#include "frame_ring.h"

#include "media_codec.h"

#include <utility>

namespace live_stream {
namespace media_source_internal {

FrameRing::~FrameRing() { Clear(); }

MediaFrameReaderId FrameRing::AttachReader(
    const MediaFrameReaderOptions &options, IFrameSink *sink,
    size_t max_readers) {
    if (readers_.size() >= max_readers) {
        return 0;
    }
    const StreamCache *cache =
        FindCache(options.stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return 0;
    }
    const MediaFrameReaderId reader_id = next_reader_id_++;
    ReaderState reader;
    reader.stream_id = options.stream_id;
    reader.keyframe_first = options.keyframe_first;
    reader.waiting_for_keyframe = options.keyframe_first && !cache->complete;
    reader.sink = sink;
    reader.reader_name = options.reader_name;
    reader.start_sequence = next_sequence_;
    reader.start_generation = cache->generation;
    reader.next_sequence = next_sequence_;
    reader.generation = cache->generation;
    readers_[reader_id] = std::move(reader);
    return reader_id;
}

bool FrameRing::DetachReader(MediaFrameReaderId reader_id,
                             MediaFrameReaderCloseReason reason) {
    auto iter = readers_.find(reader_id);
    if (iter == readers_.end()) {
        return false;
    }
    iter->second.close_reason = reason;
    ClearLiveQueue(&iter->second.live_queue);
    readers_.erase(iter);
    return true;
}

MediaFrameReaderStatus FrameRing::GetReaderStatus(
    MediaFrameReaderId reader_id) const {
    MediaFrameReaderStatus status;
    const auto reader_iter = readers_.find(reader_id);
    if (reader_iter == readers_.end()) {
        return status;
    }
    const ReaderState &reader = reader_iter->second;
    status.attached = true;
    status.stream_id = reader.stream_id;
    status.reader_generation = reader.generation;
    status.close_reason = reader.close_reason;
    status.waiting_for_keyframe = reader.waiting_for_keyframe;
    status.slow_reader = reader.live_queue.overflow;
    status.pending_frames = static_cast<uint32_t>(reader.live_queue.size);
    return status;
}

MediaFrameReaderStartData FrameRing::GetStartData(
    MediaFrameReaderId reader_id, const MediaTrack &track) const {
    MediaFrameReaderStartData start_data;
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
    start_data.stream_running = track.ready;
    start_data.gop_complete =
        cache->complete && reader.start_generation == cache->generation;
    start_data.reader_generation = reader.start_generation;
    start_data.track = track;
    if (!start_data.gop_complete) {
        return start_data;
    }
    start_data.gop_frames.reserve(cache->size);
    for (size_t i = 0; i < cache->size; ++i) {
        if (cache->frames[i].sequence >= reader.start_sequence) {
            continue;
        }
        MediaFrame media_frame;
        if (ToMediaFrame(cache->frames[i].payload, cache->frames[i].key_frame,
                         cache->frames[i].duration_us, &media_frame)) {
            start_data.gop_frames.push_back(media_frame);
        }
    }
    return start_data;
}

bool FrameRing::PopFrame(MediaFrameReaderId reader_id,
                         MediaFrameReaderFrame *frame) {
    if (frame == nullptr) {
        return false;
    }
    MediaFrameReaderFrameUnref(frame);
    auto reader_iter = readers_.find(reader_id);
    if (reader_iter == readers_.end()) {
        return false;
    }
    ReaderState &reader = reader_iter->second;
    QueuedFrame queued_frame;
    if (!PopLiveQueue(&reader.live_queue, &queued_frame)) {
        return false;
    }
    frame->reader_id = reader_id;
    frame->reader_generation = reader.generation;
    frame->starts_on_keyframe = queued_frame.starts_on_keyframe;
    const bool converted =
        ToMediaFrame(queued_frame.payload, queued_frame.key_frame,
                     queued_frame.duration_us, &frame->frame);
    FramePayloadUnref(&queued_frame.payload);
    if (!converted) {
        MediaFrameReaderFrameUnref(frame);
        return false;
    }
    return true;
}

void FrameRing::Clear() {
    for (auto &item : readers_) {
        ClearLiveQueue(&item.second.live_queue);
        item.second.close_reason = MediaFrameReaderCloseReason::kStreamStopped;
    }
    readers_.clear();
    ClearCache(&main_cache_);
    ClearCache(&sub_cache_);
    next_reader_id_ = 1;
    next_sequence_ = 1;
}

void FrameRing::ClearStream(StreamId stream_id,
                            MediaFrameReaderCloseReason reason) {
    StreamCache *cache = FindCache(stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return;
    }
    ClearCache(cache);
    ++cache->generation;
    for (auto &item : readers_) {
        ReaderState &reader = item.second;
        if (reader.stream_id == stream_id) {
            ResetReaderForStream(&reader, *cache, reason);
        }
    }
}

size_t FrameRing::ReaderCount() const { return readers_.size(); }

size_t FrameRing::SinkReaderCount() const {
    size_t count = 0;
    for (const auto &item : readers_) {
        if (item.second.sink != nullptr) {
            ++count;
        }
    }
    return count;
}

size_t FrameRing::PullReaderCount() const {
    size_t count = 0;
    for (const auto &item : readers_) {
        if (item.second.sink == nullptr) {
            ++count;
        }
    }
    return count;
}

uint32_t FrameRing::SlowReaderCount() const {
    uint32_t count = 0;
    for (const auto &item : readers_) {
        if (item.second.live_queue.overflow) {
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

FrameRingWriteResult FrameRing::Write(const FramePayload &frame) {
    FrameRingWriteResult result;
    const EncodedFrame &encoded_frame = frame.encoded_frame;
    if (!EncodedFrameHasPayload(&encoded_frame)) {
        return result;
    }

    StreamCache *cache =
        FindCache(encoded_frame.stream_id, &main_cache_, &sub_cache_);
    if (cache == nullptr) {
        return result;
    }
    const bool key_frame =
        media_codec::IsKeyFrame(encoded_frame.frame_type);
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

        if (reader.sink != nullptr) {
            PendingFrameRingWrite write;
            write.reader_id = item.first;
            write.sink = reader.sink;
            write.starts_on_keyframe = starts_on_keyframe;
            result.sink_writes.push_back(write);
            reader.close_reason = MediaFrameReaderCloseReason::kNone;
            continue;
        }
        if (!PushLiveQueue(&reader.live_queue, sequence, key_frame,
                           starts_on_keyframe, duration_us, frame)) {
            ++result.slow_reader_count;
            reader.waiting_for_keyframe = true;
            reader.next_sequence = next_sequence_;
            reader.close_reason = MediaFrameReaderCloseReason::kCacheOverflow;
        } else {
            reader.close_reason = MediaFrameReaderCloseReason::kNone;
        }
    }
    return result;
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

bool FrameRing::ToMediaFrame(const FramePayload &payload, bool key_frame,
                             int64_t duration_us, MediaFrame *frame) {
    return MediaFrameSetEncodedFrame(frame, &payload.encoded_frame,
                                     MediaTrackType::kVideo, key_frame,
                                     duration_us);
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
        ClearCache(cache);
        cache->complete = true;
        ++cache->generation;
    }
    if (!cache->complete) {
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
    if (!FramePayloadRefCopy(&cached_frame.payload, &frame)) {
        ClearCache(cache);
        ++cache->generation;
        return false;
    }
    cached_frame.sequence = sequence;
    cached_frame.key_frame = key_frame;
    cached_frame.duration_us = duration_us;
    cached_frame.bytes = frame.encoded_frame.size;
    cache->bytes += cached_frame.bytes;
    ++cache->size;
    return true;
}

void FrameRing::ResetReaderForStream(ReaderState *reader,
                                     const StreamCache &cache,
                                     MediaFrameReaderCloseReason reason) {
    if (reader == nullptr) {
        return;
    }
    reader->waiting_for_keyframe = true;
    reader->next_sequence = next_sequence_;
    reader->generation = cache.generation;
    reader->close_reason = reason;
    ClearLiveQueue(&reader->live_queue);
}

}  // namespace media_source_internal
}  // namespace live_stream
