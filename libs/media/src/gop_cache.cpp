#include "gop_cache.h"

#include <algorithm>
#include <cstdint>

namespace live_stream {
namespace media_internal {

GopCache::~GopCache() { Clear(); }

void GopCache::Clear() {
    const size_t cached_count = std::min(size_, frames_.size());
    for (size_t i = 0; i < cached_count; ++i) {
        const size_t index = (head_ + i) % frames_.size();
        MediaFlvCachedVideoTagUnref(&frames_[index]);
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
    const FlvVideoTagView &flv_tag_view) {
    if (keyframe) {
        // 新关键帧代表新的可解码 GOP 起点，旧 GOP 立即丢弃。
        Clear();
        complete_ = true;
    }
    if (size_ == 0 && !keyframe) {
        // 尚未看到关键帧时不缓存 P/B 帧；新客户端不能从这里起播。
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

void GopCache::CopyTo(MediaFlvStart *flv_start) const {
    if (flv_start == nullptr || !complete_) {
        return;
    }
    flv_start->cached_video_tags.reserve(size_);
    for (size_t i = 0; i < size_; ++i) {
        const size_t index = (head_ + i) % frames_.size();
        if (frames_[index].slice_count == 0) {
            continue;
        }
        MediaFlvCachedVideoTag cached_tag;
        if (MediaFlvCachedVideoTagRefCopy(&cached_tag, &frames_[index])) {
            flv_start->cached_video_tags.push_back(cached_tag);
        }
    }
}

bool GopCache::CopyFlvTagView(
    const EncodedFrame &frame, const FlvVideoTagView &source,
    MediaFlvCachedVideoTag *target) const {
    if (target == nullptr || !IsEncodedFramePayloadValid(&frame) ||
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
        const FlvVideoTagSlice &source_slice = source.slices[i];
        MediaFlvCachedVideoTagSlice &target_slice = cached_tag.slices[i];
        if (source_slice.data == nullptr || source_slice.size == 0) {
            MediaFlvCachedVideoTagUnref(&cached_tag);
            return false;
        }
        if (source_slice.media_payload) {
            // media payload slice 必须落在当前 EncodedFrame payload 范围内，
            // 否则缓存后指针生命周期无法保证。
            const uint8_t *payload = EncodedFramePayloadData(&frame);
            const uintptr_t payload_addr =
                reinterpret_cast<uintptr_t>(payload);
            const uintptr_t source_addr =
                reinterpret_cast<uintptr_t>(source_slice.data);
            const uintptr_t payload_offset = source_addr - payload_addr;
            if (payload == nullptr || source_addr < payload_addr ||
                payload_offset > frame.payload.size ||
                source_slice.size > frame.payload.size - payload_offset) {
                MediaFlvCachedVideoTagUnref(&cached_tag);
                return false;
            }
            target_slice.media_data = source_slice.data;
            target_slice.size = source_slice.size;
            target_slice.media_payload = true;
        } else {
            // 小 header 数据复制到 cache 自己的数组中，避免引用临时 tag view。
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

}  // namespace media_internal
}  // namespace live_stream
