#ifndef LIVE_STREAM_MEDIA_SRC_HLS_MAKER_H_
#define LIVE_STREAM_MEDIA_SRC_HLS_MAKER_H_

#include "frame_payload.h"
#include "media/media_streams.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace live_stream {
namespace media_internal {

struct HlsMakerOptions {
    uint32_t max_segments = 9;
    uint32_t max_segment_bytes = 4 * 1024 * 1024;
    uint32_t max_cached_bytes = 32 * 1024 * 1024;
};

struct TsMuxerState {
    // PAT、PMT 和 video PID 各自维护 continuity value；TS packet 丢包或
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

struct HlsFmp4Sample {
    uint32_t duration_90k = 0;
    uint32_t size = 0;
    uint32_t flags = 0;
    int32_t cts_offset_90k = 0;
};

    // 管理单路码流的内存 HLS segment 生命周期。
// 当前 segment 只有在 FinalizeCurrentSegment 后才进入 playlist，对外不可见。
class HlsMaker {
public:
    HlsMaker() = default;
    HlsMaker(const HlsMaker &) = delete;
    HlsMaker &operator=(const HlsMaker &) = delete;
    ~HlsMaker();

    void Configure(const HlsMakerOptions &options);
    void Reset();
    bool IsPlaylistReady() const;
    size_t SegmentSize() const;
    uint64_t FirstSegmentSequence() const;
    uint64_t LastSegmentSequence() const;
    uint64_t MissingSegments() const;
    uint64_t EvictedSegments() const;
    uint32_t CurrentSegmentSize() const;
    uint32_t CachedBytes() const;
    uint64_t DropSize() const;
    const std::string &CodecString() const;
    MediaHlsPlaylist BuildPlaylist(uint32_t hls_segment_duration_ms,
                                   uint32_t hls_playlist_depth) const;
    MediaSegmentRef FindSegmentRef(uint64_t sequence) const;
    void RecordSegmentMiss() const;
    bool AppendFrame(const MediaFrame &frame,
                     const FramePayload &payload,
                     const std::string &vps,
                     const std::string &sps,
                     const std::string &pps,
                     bool keyframe,
                     bool prepend_parameter_sets,
                     uint32_t hls_segment_duration_ms,
                     bool &segment_created);

private:
    struct SegmentState {
        bool started = false;
        HlsSegmentFormat format = HlsSegmentFormat::kTs;
        uint64_t sequence = 0;
        int64_t start_pts_us = 0;
        int64_t start_dts_us = 0;
        int64_t last_pts_us = 0;
        int64_t last_dts_us = 0;
        uint64_t base_decode_time_90k = 0;
        MediaBufferBuilder body;
        std::vector<HlsFmp4Sample> samples;
    };

    static void ResetSegmentState(SegmentState &segment);
    static TsSegmentBuffer SegmentBuffer(SegmentState &segment);
    static bool CommitSegmentBuffer(
        SegmentState &segment,
        const TsSegmentBuffer &buffer);

    uint32_t ClampSegmentCapacity(size_t capacity) const;
    bool EnsureSegmentCapacity(SegmentState &segment,
                               size_t extra_bytes) const;
    void ClearSegments();
    void ObserveFrameTiming(const MediaFrame &frame);
    bool AppendFrameToSegment(const FramePayload &payload,
                              const std::string &vps,
                              const std::string &sps,
                              const std::string &pps,
                              bool prepend_parameter_sets,
                              const MediaFrame &frame);
    bool BuildFinalizedFmp4Segment(const SegmentState &segment,
                                   MediaBufferRef &body) const;
    int64_t CurrentSegmentDurationUs() const;
    void StartSegment(Codec codec, int64_t pts_us, int64_t dts_us);
    void RememberSegmentCapacity(const SegmentState &segment);
    void PopOldestSegment();
    bool PushFinalizedSegment();
    bool FinalizeCurrentSegment();

    HlsMakerOptions options_;
    std::deque<MediaSegmentRef> segments_;
    SegmentState current_segment_;
    MediaBufferRef init_segment_;
    std::string codec_string_;
    TsMuxerState ts_muxer_state_;
    uint32_t next_segment_capacity_ = 0;
    uint64_t next_segment_sequence_ = 1;
    mutable uint64_t missing_segments_ = 0;
    uint64_t evicted_segments_ = 0;
    uint32_t cached_bytes_ = 0;
    uint64_t drop_size_ = 0;
    int64_t last_pts_us_ = -1;
    int64_t last_frame_duration_us_ = 33333;
    bool h265_init_logged_ = false;
    bool h265_first_segment_logged_ = false;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_HLS_MAKER_H_
