#include "frame_ring.h"

#include "stream_codec.h"

#include <utility>

namespace live_stream {
namespace media_source_internal {

FrameRing::~FrameRing() { Clear(); }

FrameAttachId FrameRing::AttachReader(const FrameAttachOptions &options,
                                      IFrameSink *sink,
                                      size_t max_readers) {
    if (sink == nullptr || readers_.size() >= max_readers) {
        return 0;
    }
    const FrameAttachId reader_id = next_reader_id_++;
    ReaderState reader;
    reader.stream_id = options.stream_id;
    reader.require_key_frame_first = options.require_key_frame_first;
    reader.sink = sink;
    reader.sink_name = options.sink_name;
    reader.next_sequence = next_sequence_;
    readers_[reader_id] = std::move(reader);
    return reader_id;
}

bool FrameRing::DetachReader(FrameAttachId reader_id) {
    return readers_.erase(reader_id) != 0;
}

void FrameRing::Clear() {
    readers_.clear();
    ClearCache(&main_cache_);
    ClearCache(&sub_cache_);
    next_reader_id_ = 1;
    next_sequence_ = 1;
}

void FrameRing::ClearStreamCache(StreamId stream_id) {
    ClearCache(FindCache(stream_id, &main_cache_, &sub_cache_));
    for (auto &item : readers_) {
        if (item.second.stream_id == stream_id) {
            item.second.require_key_frame_first = true;
            item.second.next_sequence = next_sequence_;
        }
    }
}

size_t FrameRing::ReaderCount() const { return readers_.size(); }

std::vector<PendingFrameRingWrite> FrameRing::Write(
    const FramePayload &frame) {
    std::vector<PendingFrameRingWrite> writes;
    const EncodedFrame &encoded_frame = frame.encoded_frame;
    if (!EncodedFrameHasPayload(&encoded_frame)) {
        return writes;
    }

    const bool key_frame =
        stream_codec::IsKeyFrame(encoded_frame.frame_type);
    const uint64_t sequence = next_sequence_++;
    (void)AppendToCache(
        FindCache(encoded_frame.stream_id, &main_cache_, &sub_cache_),
        sequence, key_frame, frame);

    for (auto &item : readers_) {
        ReaderState &reader = item.second;
        if (reader.stream_id != encoded_frame.stream_id ||
            reader.sink == nullptr) {
            continue;
        }
        if (reader.require_key_frame_first && !key_frame) {
            continue;
        }
        PendingFrameRingWrite write;
        write.reader_id = item.first;
        write.sink = reader.sink;
        write.starts_on_keyframe = reader.require_key_frame_first && key_frame;
        reader.require_key_frame_first = false;
        reader.next_sequence = sequence + 1;
        writes.push_back(write);
    }
    return writes;
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
    }
    cache->size = 0;
    cache->complete = false;
}

bool FrameRing::AppendToCache(StreamCache *cache, uint64_t sequence,
                              bool key_frame, const FramePayload &frame) {
    if (cache == nullptr) {
        return false;
    }
    if (key_frame) {
        ClearCache(cache);
        cache->complete = true;
    }
    if (!cache->complete) {
        return true;
    }
    if (cache->size >= cache->frames.size()) {
        ClearCache(cache);
        return false;
    }
    CachedFrame &cached_frame = cache->frames[cache->size];
    FramePayloadUnref(&cached_frame.payload);
    if (!FramePayloadRefCopy(&cached_frame.payload, &frame)) {
        ClearCache(cache);
        return false;
    }
    cached_frame.sequence = sequence;
    cached_frame.key_frame = key_frame;
    ++cache->size;
    return true;
}

}  // namespace media_source_internal
}  // namespace live_stream
