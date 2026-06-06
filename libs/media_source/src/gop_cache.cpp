#include "gop_cache.h"

#include <algorithm>
#include <cstdint>

namespace live_stream {
namespace media_source_internal {

GopCache::~GopCache() { Clear(); }

void GopCache::Clear() {
    for (MediaFlvCachedVideoTag &cached_tag : frames_) {
        MediaFlvCachedVideoTagUnref(&cached_tag);
    }
    head_ = 0;
    size_ = 0;
    complete_ = false;
}

uint32_t GopCache::FirstFlvTagSize() const {
    if (!complete_ || size_ == 0) {
        return 0;
    }
    return static_cast<uint32_t>(frames_[head_].total_size);
}

bool GopCache::AppendFlvTag(
    const EncodedFrame &frame, bool keyframe,
    const stream_mux::FlvVideoTagView &flv_tag_view) {
    if (keyframe) {
        Clear();
        complete_ = true;
    }
    if (size_ == 0 && !keyframe) {
        return true;
    }
    if (size_ >= frames_.size()) {
        Clear();
        return false;
    }
    const size_t index = (head_ + size_) % frames_.size();
    if (!CopyFlvTagView(frame, flv_tag_view, &frames_[index])) {
        Clear();
        return false;
    }
    ++size_;
    return true;
}

void GopCache::CopyTo(MediaFlvStartData *start_data) const {
    if (start_data == nullptr || !complete_) {
        return;
    }
    start_data->cached_video_tags.reserve(size_);
    for (size_t i = 0; i < size_; ++i) {
        const size_t index = (head_ + i) % frames_.size();
        if (frames_[index].slice_count == 0) {
            continue;
        }
        MediaFlvCachedVideoTag cached_tag;
        if (MediaFlvCachedVideoTagRefCopy(&cached_tag, &frames_[index])) {
            start_data->cached_video_tags.push_back(cached_tag);
        }
    }
}

bool GopCache::CopyFlvTagView(
    const EncodedFrame &frame, const stream_mux::FlvVideoTagView &source,
    MediaFlvCachedVideoTag *target) const {
    if (target == nullptr || !EncodedFrameHasPayload(&frame) ||
        source.slice_count == 0 ||
        source.slice_count > kMaxMediaFlvVideoTagSlices) {
        return false;
    }

    MediaFlvCachedVideoTag cached_tag;
    if (!EncodedFrameRefCopy(&cached_tag.frame, &frame)) {
        return false;
    }
    cached_tag.slice_count = source.slice_count;
    cached_tag.total_size = source.total_size;
    cached_tag.timestamp_ms = source.timestamp_ms;
    for (size_t i = 0; i < source.slice_count; ++i) {
        const stream_mux::FlvVideoTagSlice &source_slice = source.slices[i];
        MediaFlvCachedVideoTagSlice &target_slice = cached_tag.slices[i];
        if (source_slice.data == nullptr || source_slice.size == 0) {
            MediaFlvCachedVideoTagUnref(&cached_tag);
            return false;
        }
        if (source_slice.media_payload) {
            const uint8_t *payload = EncodedFramePayloadData(&frame);
            const uintptr_t payload_addr =
                reinterpret_cast<uintptr_t>(payload);
            const uintptr_t source_addr =
                reinterpret_cast<uintptr_t>(source_slice.data);
            if (payload == nullptr || source_addr < payload_addr ||
                source_addr - payload_addr > frame.size ||
                source_slice.size > frame.size - (source_addr - payload_addr)) {
                MediaFlvCachedVideoTagUnref(&cached_tag);
                return false;
            }
            target_slice.media_data = source_slice.data;
            target_slice.size = source_slice.size;
            target_slice.media_payload = true;
        } else {
            if (source_slice.size > sizeof(target_slice.header_data)) {
                MediaFlvCachedVideoTagUnref(&cached_tag);
                return false;
            }
            std::copy(source_slice.data, source_slice.data + source_slice.size,
                      target_slice.header_data);
            target_slice.size = source_slice.size;
            target_slice.media_payload = false;
        }
    }

    MediaFlvCachedVideoTagUnref(target);
    *target = cached_tag;
    return true;
}

}  // namespace media_source_internal
}  // namespace live_stream
