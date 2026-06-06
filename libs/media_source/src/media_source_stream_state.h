#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_MEDIA_SOURCE_STREAM_STATE_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_MEDIA_SOURCE_STREAM_STATE_H_

#include "media_source.h"

#include "media/frame_attach.h"
#include "stream_codec.h"
#include "stream_mux.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace live_stream {
namespace media_source_internal {

struct HlsSegmentState {
    bool started = false;
    uint64_t sequence = 0;
    int64_t start_pts_us = 0;
    int64_t last_pts_us = 0;
    VideoBuffer *body = nullptr;
};

struct CachedFlvFrameRing {
    std::array<MediaFlvCachedVideoTag, kMaxMediaFlvCachedVideoTags> frames;
    size_t head = 0;
    size_t size = 0;
    bool complete = false;
};

// 单路码流的浏览器播放状态。服务层只负责加锁和分发，HLS/FLV 的
// 参数集、分片缓存和打包游标都集中维护在这里。
struct StreamContext {
    VideoCodec codec = VideoCodec::kH264;
    StreamState state = StreamState::kClosed;
    std::string vps;
    std::string sps;
    std::string pps;
    std::string sequence_header_tag;
    CachedFlvFrameRing flv_gop_cache;
    std::deque<MediaSegmentRef> segments;
    HlsSegmentState current_segment;
    uint32_t next_hls_segment_capacity = 0;
    mutable bool hls_requested = false;
    uint64_t next_segment_sequence = 1;
    uint64_t config_generation = 0;
    stream_mux::TsMuxerState ts_muxer_state;
    int64_t last_pts_us = -1;
    int64_t last_frame_duration_us = 33333;
    bool timestamp_base_set = false;
    int64_t timestamp_base_dts_us = 0;
    int64_t last_output_dts_us = -1;
    int64_t last_output_pts_us = -1;
};

using ParsedFramePayload = FramePayload;

struct PackagedFrameResult {
    bool accepted = false;
    bool keyframe = false;
    bool hls_segment_created = false;
    stream_mux::FlvVideoTagView flv_tag_view;
    bool has_flv_tag_view = false;
};

bool IsBrowserStreamReady(StreamState state, VideoCodec codec);
bool IsBrowserCodec(VideoCodec codec);
bool IsFlvCodecSupported(VideoCodec codec);
bool IsHlsCodecSupported(VideoCodec codec);
bool IsMjpegCodecSupported(VideoCodec codec);
bool HasFlvSequenceHeader(const StreamContext &stream);
bool IsFlvStreamReady(const StreamContext &stream);
bool IsHlsStreamReady(const StreamContext &stream);
bool IsMjpegStreamReady(const StreamContext &stream);

void ParseFramePayload(const EncodedFrame &frame, ParsedFramePayload *payload);
bool HasParsedUnits(const ParsedFramePayload &payload);
void ParsedFramePayloadUnref(ParsedFramePayload *payload);
void ClearStreamContext(StreamContext *stream);

MediaHlsPlaylist BuildHlsPlaylist(const StreamContext &stream,
                                  uint32_t hls_segment_duration_ms,
                                  uint32_t hls_playlist_depth);
MediaSegmentRef FindHlsSegmentRef(const StreamContext &stream,
                                  uint64_t sequence);
MediaFlvStartData BuildFlvStartData(const StreamContext &stream);

void ResetStream(StreamContext *stream, VideoCodec codec);
bool NormalizeFrameTimestamps(StreamContext *stream, EncodedFrame *frame);
PackagedFrameResult AppendFrameToStream(StreamContext *stream,
                                        const EncodedFrame &frame,
                                        const ParsedFramePayload &payload,
                                        bool package_hls,
                                        bool package_flv,
                                        uint32_t hls_segment_duration_ms,
                                        uint32_t hls_playlist_depth);

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_MEDIA_SOURCE_STREAM_STATE_H_
