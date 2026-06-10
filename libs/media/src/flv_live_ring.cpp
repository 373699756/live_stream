#include "flv_live_ring.h"

#include <utility>

namespace live_stream {
namespace media_internal {

MediaFlvClientId FlvLiveRing::AttachReader(
    StreamId stream_id, uint64_t config_generation, bool wait_for_keyframe,
    IMediaFlvSink *sink, size_t max_readers) {
    if (sink == nullptr || readers_.size() >= max_readers) {
        return 0;
    }
    const MediaFlvClientId client_id = next_reader_id_++;
    ReaderState reader;
    reader.stream_id = stream_id;
    reader.config_generation = config_generation;
    reader.wait_for_keyframe = wait_for_keyframe;
    reader.sink = sink;
    readers_[client_id] = reader;
    return client_id;
}

bool FlvLiveRing::DetachReader(MediaFlvClientId client_id) {
    auto iter = readers_.find(client_id);
    if (iter == readers_.end()) {
        return false;
    }
    iter->second.detached = true;
    (void)EraseDetachedReader(client_id, &iter->second);
    return true;
}

void FlvLiveRing::Clear() {
    for (auto iter = readers_.begin(); iter != readers_.end();) {
        iter->second.detached = true;
        if (iter->second.pending_writes == 0) {
            ReleaseReaderSink(&iter->second);
            iter = readers_.erase(iter);
        } else {
            ++iter;
        }
    }
}

size_t FlvLiveRing::ReaderCount() const { return readers_.size(); }

bool FlvLiveRing::HasReader(StreamId stream_id) const {
    for (const auto &item : readers_) {
        if (!item.second.detached && item.second.stream_id == stream_id &&
            item.second.sink != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<PendingFlvClientWrite> FlvLiveRing::CollectWrites(
    StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
    bool has_sequence_header, bool keyframe) {
    std::vector<PendingFlvClientWrite> writes;
    if (!has_flv_tag || !has_sequence_header) {
        return writes;
    }
    for (auto &item : readers_) {
        ReaderState &reader = item.second;
        if (reader.detached || reader.stream_id != stream_id ||
            reader.sink == nullptr) {
            continue;
        }
        const bool starts_on_keyframe = reader.wait_for_keyframe && keyframe;
        if (reader.wait_for_keyframe && !keyframe) {
            continue;
        }
        if (keyframe) {
            reader.wait_for_keyframe = false;
        }
        const bool needs_config =
            (starts_on_keyframe ||
             reader.config_generation != config_generation) &&
            has_sequence_header;
        if (needs_config) {
            reader.config_generation = config_generation;
        }
        PendingFlvClientWrite write;
        write.client_id = item.first;
        write.sink = reader.sink;
        write.send_sequence_header = needs_config;
        write.starts_on_keyframe = starts_on_keyframe;
        writes.push_back(std::move(write));
        ++reader.pending_writes;
    }
    return writes;
}

void FlvLiveRing::ReleaseWrite(MediaFlvClientId client_id) {
    auto iter = readers_.find(client_id);
    if (iter == readers_.end()) {
        return;
    }
    if (iter->second.pending_writes > 0) {
        --iter->second.pending_writes;
    }
    (void)EraseDetachedReader(client_id, &iter->second);
}

void FlvLiveRing::ReleaseReaderSink(ReaderState *reader) {
    if (reader == nullptr || reader->sink == nullptr) {
        return;
    }
    delete reader->sink;
    reader->sink = nullptr;
}

bool FlvLiveRing::EraseDetachedReader(MediaFlvClientId client_id,
                                      ReaderState *reader) {
    if (reader == nullptr || !reader->detached ||
        reader->pending_writes != 0) {
        return false;
    }
    ReleaseReaderSink(reader);
    return readers_.erase(client_id) != 0;
}

}  // namespace media_internal
}  // namespace live_stream
