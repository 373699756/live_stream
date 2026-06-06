#include "hls_maker.h"

#include <algorithm>

namespace live_stream {
namespace media_source_internal {
namespace {

constexpr size_t kInitialHlsSegmentBytes = 256 * 1024;
constexpr size_t kMaxHlsSegmentBytes = 4 * 1024 * 1024;

}  // namespace

HlsMaker::~HlsMaker() { Reset(); }

void HlsMaker::Reset() {
    ClearSegments();
    UnrefSegmentState(&current_segment_);
    ts_muxer_state_ = media_mux::TsMuxerState{};
    next_segment_capacity_ = 0;
    next_segment_sequence_ = 1;
    last_pts_us_ = -1;
    last_frame_duration_us_ = 33333;
    requested_ = false;
}

void HlsMaker::MarkRequested() const { requested_ = true; }

bool HlsMaker::Requested() const { return requested_; }

bool HlsMaker::HasSegments() const { return !segments_.empty(); }

size_t HlsMaker::SegmentCount() const { return segments_.size(); }

uint32_t HlsMaker::CurrentSegmentSize() const {
    return current_segment_.body != nullptr ? current_segment_.body->size : 0;
}

MediaHlsPlaylist HlsMaker::BuildPlaylist(
    uint32_t hls_segment_duration_ms, uint32_t hls_playlist_depth) const {
    MediaHlsPlaylist playlist;
    if (segments_.empty()) {
        return playlist;
    }

    playlist.supported = true;
    const size_t playlist_depth =
        std::max<size_t>(1, static_cast<size_t>(hls_playlist_depth));
    const size_t start_index =
        segments_.size() > playlist_depth
            ? segments_.size() - playlist_depth
            : 0;
    playlist.media_sequence = segments_[start_index].sequence;
    int64_t max_duration_us =
        static_cast<int64_t>(hls_segment_duration_ms) * 1000;
    for (size_t i = start_index; i < segments_.size(); ++i) {
        const MediaSegmentRef &segment = segments_[i];
        playlist.entries.push_back(
            MediaHlsEntry{segment.sequence, segment.duration_us});
        max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    playlist.target_duration_sec = static_cast<uint32_t>(
        std::max<int64_t>(1, (max_duration_us + 999999) / 1000000));
    return playlist;
}

MediaSegmentRef HlsMaker::FindSegmentRef(uint64_t sequence) const {
    for (const MediaSegmentRef &segment : segments_) {
        if (segment.sequence == sequence) {
            return MediaSegmentRefCopy(&segment);
        }
    }
    return MediaSegmentRef{};
}

bool HlsMaker::AppendFrame(const EncodedFrame &frame,
                           const FramePayload &payload,
                           const std::string &vps,
                           const std::string &sps,
                           const std::string &pps,
                           bool keyframe,
                           bool prepend_parameter_sets,
                           uint32_t hls_segment_duration_ms,
                           uint32_t hls_segment_cache_depth,
                           bool *segment_created) {
    if (segment_created != nullptr) {
        *segment_created = false;
    }
    ObserveFrameTiming(frame);

    if (keyframe && current_segment_.started &&
        frame.pts_us - current_segment_.start_pts_us >=
            static_cast<int64_t>(hls_segment_duration_ms) * 1000) {
        const bool finalized =
            FinalizeCurrentSegment(hls_segment_cache_depth);
        if (segment_created != nullptr) {
            *segment_created = finalized;
        }
    }
    if (keyframe && !current_segment_.started) {
        StartSegment(frame.codec, frame.pts_us);
    }
    if (!current_segment_.started) {
        return true;
    }
    if (!AppendFrameToSegment(payload, vps, sps, pps,
                              prepend_parameter_sets, frame)) {
        UnrefSegmentState(&current_segment_);
        return false;
    }
    current_segment_.last_pts_us = frame.pts_us;
    return true;
}

void HlsMaker::UnrefSegmentState(SegmentState *segment) {
    if (segment == nullptr) {
        return;
    }
    VideoBufferUnref(segment->body);
    *segment = SegmentState{};
}

uint32_t HlsMaker::ClampSegmentCapacity(size_t capacity) {
    if (capacity < kInitialHlsSegmentBytes) {
        return static_cast<uint32_t>(kInitialHlsSegmentBytes);
    }
    if (capacity > kMaxHlsSegmentBytes) {
        return static_cast<uint32_t>(kMaxHlsSegmentBytes);
    }
    return static_cast<uint32_t>(capacity);
}

bool HlsMaker::EnsureSegmentCapacity(SegmentState *segment,
                                     size_t extra_bytes) {
    if (segment == nullptr || segment->body == nullptr ||
        segment->body->size > segment->body->capacity) {
        return false;
    }
    if (extra_bytes <= segment->body->capacity - segment->body->size) {
        return true;
    }
    uint32_t new_capacity = segment->body->capacity;
    while (extra_bytes > new_capacity - segment->body->size) {
        if (new_capacity >= kMaxHlsSegmentBytes) {
            return false;
        }
        const uint32_t doubled = new_capacity * 2U;
        new_capacity = doubled > new_capacity ? doubled : kMaxHlsSegmentBytes;
        if (new_capacity > kMaxHlsSegmentBytes) {
            new_capacity = kMaxHlsSegmentBytes;
        }
    }
    VideoBuffer *new_body = VideoBufferAlloc(new_capacity);
    if (new_body == nullptr) {
        return false;
    }
    std::copy(segment->body->data, segment->body->data + segment->body->size,
              new_body->data);
    if (!VideoBufferSetSize(new_body, segment->body->size)) {
        VideoBufferUnref(new_body);
        return false;
    }
    VideoBufferUnref(segment->body);
    segment->body = new_body;
    return true;
}

media_mux::TsSegmentBuffer HlsMaker::SegmentBuffer(
    SegmentState *segment) {
    media_mux::TsSegmentBuffer buffer;
    if (segment == nullptr || segment->body == nullptr) {
        return buffer;
    }
    buffer.data = segment->body->data;
    buffer.capacity = segment->body->capacity;
    buffer.size = segment->body->size;
    return buffer;
}

bool HlsMaker::CommitSegmentBuffer(
    SegmentState *segment,
    const media_mux::TsSegmentBuffer &buffer) {
    return segment != nullptr && segment->body != nullptr &&
           buffer.size <= segment->body->capacity &&
           VideoBufferSetSize(segment->body,
                              static_cast<uint32_t>(buffer.size));
}

void HlsMaker::ClearSegments() {
    for (MediaSegmentRef &segment : segments_) {
        MediaSegmentRefUnref(&segment);
    }
    segments_.clear();
}

void HlsMaker::ObserveFrameTiming(const EncodedFrame &frame) {
    if (last_pts_us_ > 0 && frame.pts_us > last_pts_us_) {
        last_frame_duration_us_ = frame.pts_us - last_pts_us_;
    }
    last_pts_us_ = frame.pts_us;
}

bool HlsMaker::AppendFrameToSegment(const FramePayload &payload,
                                    const std::string &vps,
                                    const std::string &sps,
                                    const std::string &pps,
                                    bool prepend_parameter_sets,
                                    const EncodedFrame &frame) {
    if (current_segment_.body == nullptr) {
        return false;
    }
    for (size_t attempt = 0; attempt < 8; ++attempt) {
        media_mux::TsSegmentBuffer segment_body =
            SegmentBuffer(&current_segment_);
        const size_t original_size = segment_body.size;
        media_mux::TsMuxerState original_state = ts_muxer_state_;
        bool appended = false;
        if (frame.codec == VideoCodec::kH265) {
            appended = media_mux::AppendH265NalUnitsToTsSegmentBuffer(
                payload.h265_units, vps, sps, pps, prepend_parameter_sets,
                frame.pts_us, frame.dts_us,
                &ts_muxer_state_, &segment_body);
        } else {
            appended = media_mux::AppendH264NalUnitsToTsSegmentBuffer(
                payload.h264_units, sps, pps, prepend_parameter_sets,
                frame.pts_us, frame.dts_us, &ts_muxer_state_, &segment_body);
        }
        if (appended && CommitSegmentBuffer(&current_segment_, segment_body)) {
            return true;
        }
        ts_muxer_state_ = original_state;
        (void)VideoBufferSetSize(current_segment_.body,
                                 static_cast<uint32_t>(original_size));
        if (!EnsureSegmentCapacity(&current_segment_,
                                   current_segment_.body->capacity)) {
            return false;
        }
    }
    return false;
}

int64_t HlsMaker::CurrentSegmentDurationUs() const {
    return std::max<int64_t>(last_frame_duration_us_,
                             current_segment_.last_pts_us -
                                 current_segment_.start_pts_us +
                                 last_frame_duration_us_);
}

void HlsMaker::StartSegment(VideoCodec codec, int64_t pts_us) {
    UnrefSegmentState(&current_segment_);
    current_segment_ = SegmentState{};
    current_segment_.started = true;
    current_segment_.sequence = next_segment_sequence_++;
    current_segment_.start_pts_us = pts_us;
    current_segment_.last_pts_us = pts_us;
    const uint32_t segment_capacity =
        ClampSegmentCapacity(next_segment_capacity_);
    current_segment_.body = VideoBufferAlloc(segment_capacity);
    if (current_segment_.body == nullptr) {
        UnrefSegmentState(&current_segment_);
        return;
    }
    media_mux::TsSegmentBuffer segment_body =
        SegmentBuffer(&current_segment_);
    if (!media_mux::AppendTsSegmentHeader(codec, &ts_muxer_state_,
                                           &segment_body) ||
        !CommitSegmentBuffer(&current_segment_, segment_body)) {
        UnrefSegmentState(&current_segment_);
    }
}

void HlsMaker::RememberSegmentCapacity(const SegmentState &segment) {
    if (segment.body == nullptr) {
        return;
    }
    next_segment_capacity_ = ClampSegmentCapacity(segment.body->size);
}

void HlsMaker::PopOldestSegment() {
    if (segments_.empty()) {
        return;
    }
    MediaSegmentRefUnref(&segments_.front());
    segments_.pop_front();
}

void HlsMaker::PushFinalizedSegment(uint32_t segment_cache_depth) {
    if (!current_segment_.started || current_segment_.body == nullptr ||
        current_segment_.body->size == 0) {
        return;
    }
    MediaSegmentRef segment;
    segment.found = true;
    segment.sequence = current_segment_.sequence;
    segment.duration_us = CurrentSegmentDurationUs();
    segment.body = current_segment_.body;
    RememberSegmentCapacity(current_segment_);
    current_segment_.body = nullptr;
    segments_.push_back(segment);
    while (segments_.size() > segment_cache_depth) {
        PopOldestSegment();
    }
}

bool HlsMaker::FinalizeCurrentSegment(uint32_t segment_cache_depth) {
    if (!current_segment_.started || current_segment_.body == nullptr ||
        current_segment_.body->size == 0) {
        return false;
    }

    PushFinalizedSegment(segment_cache_depth);
    UnrefSegmentState(&current_segment_);
    return true;
}

}  // namespace media_source_internal
}  // namespace live_stream
