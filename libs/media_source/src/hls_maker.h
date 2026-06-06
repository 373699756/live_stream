#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_HLS_MAKER_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_HLS_MAKER_H_

#include "media/frame_attach.h"
#include "media_source.h"
#include "stream_mux.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace live_stream {
namespace media_source_internal {

// Owns the in-memory MPEG-TS HLS segment lifecycle for one media stream.
// Segments become visible to playlists only after FinalizeCurrentSegment().
class HlsMaker {
public:
    HlsMaker() = default;
    HlsMaker(const HlsMaker &) = delete;
    HlsMaker &operator=(const HlsMaker &) = delete;
    ~HlsMaker();

    void Reset();
    void MarkRequested() const;
    bool Requested() const;
    bool HasSegments() const;
    size_t SegmentCount() const;
    uint32_t CurrentSegmentSize() const;
    MediaHlsPlaylist BuildPlaylist(uint32_t hls_segment_duration_ms,
                                   uint32_t hls_playlist_depth) const;
    MediaSegmentRef FindSegmentRef(uint64_t sequence) const;
    bool AppendFrame(const EncodedFrame &frame,
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
        VideoBuffer *body = nullptr;
    };

    static void UnrefSegmentState(SegmentState *segment);
    static uint32_t ClampSegmentCapacity(size_t capacity);
    static bool EnsureSegmentCapacity(SegmentState *segment,
                                      size_t extra_bytes);
    static stream_mux::TsSegmentBuffer SegmentBuffer(
        SegmentState *segment);
    static bool CommitSegmentBuffer(
        SegmentState *segment,
        const stream_mux::TsSegmentBuffer &buffer);

    void ClearSegments();
    void ObserveFrameTiming(const EncodedFrame &frame);
    bool AppendFrameToSegment(const FramePayload &payload,
                              const std::string &vps,
                              const std::string &sps,
                              const std::string &pps,
                              bool prepend_parameter_sets,
                              const EncodedFrame &frame);
    int64_t CurrentSegmentDurationUs() const;
    void StartSegment(VideoCodec codec, int64_t pts_us);
    void RememberSegmentCapacity(const SegmentState &segment);
    void PopOldestSegment();
    void PushFinalizedSegment(uint32_t segment_cache_depth);
    bool FinalizeCurrentSegment(uint32_t segment_cache_depth);

    std::deque<MediaSegmentRef> segments_;
    SegmentState current_segment_;
    stream_mux::TsMuxerState ts_muxer_state_;
    uint32_t next_segment_capacity_ = 0;
    uint64_t next_segment_sequence_ = 1;
    int64_t last_pts_us_ = -1;
    int64_t last_frame_duration_us_ = 33333;
    mutable bool requested_ = false;
};

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_HLS_MAKER_H_
