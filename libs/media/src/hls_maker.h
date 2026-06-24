#ifndef LIVE_STREAM_MEDIA_SRC_HLS_MAKER_H_
#define LIVE_STREAM_MEDIA_SRC_HLS_MAKER_H_

#include "frame_payload.h"
#include "media/media_streams.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace live_stream {
namespace media_internal {

struct TsMuxerState {
    // PAT、PMT 和 video PID 各自维护 continuity counter；TS packet 丢包或
    // 重排诊断会依赖这些 4 bit 计数。
    uint8_t pat_continuity = 0;
    uint8_t pmt_continuity = 0;
    uint8_t video_continuity = 0;
};

struct TsSegmentBuffer {
    uint8_t *data = nullptr;
    size_t capacity = 0;
    size_t size = 0;
};

// 管理单路码流的内存 MPEG-TS HLS segment 生命周期。
// 当前 segment 只有在 FinalizeCurrentSegment 后才进入 playlist，对外不可见。
class HlsMaker {
public:
    HlsMaker() = default;
    HlsMaker(const HlsMaker &) = delete;
    HlsMaker &operator=(const HlsMaker &) = delete;
    ~HlsMaker();

    void Reset();
    void MarkRequested() const;
    bool Requested() const;
    bool IsPlaylistReady() const;
    size_t SegmentSize() const;
    uint64_t FirstSegmentSequence() const;
    uint64_t LastSegmentSequence() const;
    uint64_t MissingSegments() const;
    uint64_t EvictedSegments() const;
    uint32_t CurrentSegmentSize() const;
    MediaHlsPlaylist BuildPlaylist(uint32_t hls_segment_duration_ms,
                                   uint32_t hls_playlist_depth) const;
    MediaSegmentRef FindSegmentRef(uint64_t sequence) const;
    bool AppendFrame(const MediaFrame &frame,
                     const FramePayload &payload,
                     const std::string &vps,
                     const std::string &sps,
                     const std::string &pps,
                     bool keyframe,
                     bool prepend_parameter_sets,
                     uint32_t hls_segment_duration_ms,
                     uint32_t hls_segment_cache_depth,
                     bool *segment_created);

private:
    struct SegmentState {
        bool started = false;
        uint64_t sequence = 0;
        int64_t start_pts_us = 0;
        int64_t last_pts_us = 0;
        MediaBufferBuilder body;
    };

    static void ResetSegmentState(SegmentState *segment);
    static uint32_t ClampSegmentCapacity(size_t capacity);
    static bool EnsureSegmentCapacity(SegmentState *segment,
                                      size_t extra_bytes);
    static TsSegmentBuffer SegmentBuffer(SegmentState *segment);
    static bool CommitSegmentBuffer(
        SegmentState *segment,
        const TsSegmentBuffer &buffer);

    void ClearSegments();
    void ObserveFrameTiming(const MediaFrame &frame);
    bool AppendFrameToSegment(const FramePayload &payload,
                              const std::string &vps,
                              const std::string &sps,
                              const std::string &pps,
                              bool prepend_parameter_sets,
                              const MediaFrame &frame);
    int64_t CurrentSegmentDurationUs() const;
    void StartSegment(Codec codec, int64_t pts_us);
    void RememberSegmentCapacity(const SegmentState &segment);
    void PopOldestSegment();
    void PushFinalizedSegment(uint32_t segment_cache_depth);
    bool FinalizeCurrentSegment(uint32_t segment_cache_depth);

    std::deque<MediaSegmentRef> segments_;
    SegmentState current_segment_;
    TsMuxerState ts_muxer_state_;
    uint32_t next_segment_capacity_ = 0;
    uint64_t next_segment_sequence_ = 1;
    mutable uint64_t missing_segment_count_ = 0;
    uint64_t evicted_segment_count_ = 0;
    int64_t last_pts_us_ = -1;
    int64_t last_frame_duration_us_ = 33333;
    mutable bool requested_ = false;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_HLS_MAKER_H_
