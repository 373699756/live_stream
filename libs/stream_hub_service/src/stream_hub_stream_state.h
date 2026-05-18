#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_SRC_STREAM_HUB_STREAM_STATE_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_SRC_STREAM_HUB_STREAM_STATE_H_

#include "media/encoded_frame.h"
#include "stream_codec.h"
#include "stream_hub_service.h"
#include "stream_mux.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace live_stream {
namespace stream_hub_internal {

struct HlsSegmentState {
  bool started = false;
  uint64_t sequence = 0;
  int64_t start_pts_us = 0;
  int64_t last_pts_us = 0;
  std::string body;
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
  std::string last_keyframe_tag;
  std::deque<StreamSegment> segments;
  HlsSegmentState current_segment;
  uint64_t next_segment_sequence = 1;
  uint64_t config_generation = 0;
  stream_mux::TsMuxerState ts_muxer_state;
  int64_t last_pts_us = -1;
  int64_t last_frame_duration_us = 33333;
};

struct ParsedFramePayload {
  VideoCodec codec = VideoCodec::kH264;
  std::vector<stream_codec::H264NalUnit> h264_units;
  std::vector<stream_codec::H265NalUnit> h265_units;
};

struct PackagedFrameResult {
  bool accepted = false;
  bool hls_segment_created = false;
  std::string sequence_header_tag;
  std::string flv_tag;
};

bool IsBrowserStreamReady(StreamState state, VideoCodec codec);

ParsedFramePayload ParseFramePayload(const EncodedFrame &frame);
bool HasParsedUnits(const ParsedFramePayload &payload);

StreamHlsPlaylist BuildHlsPlaylist(const StreamContext &stream,
                                   uint32_t hls_segment_duration_ms);
StreamSegment FindHlsSegment(const StreamContext &stream, uint64_t sequence);
StreamFlvBootstrap BuildFlvBootstrap(const StreamContext &stream);

void ResetStream(StreamContext *stream, VideoCodec codec);
PackagedFrameResult AppendFrameToStream(StreamContext *stream,
                                        const EncodedFrame &frame,
                                        const ParsedFramePayload &payload,
                                        uint32_t hls_segment_duration_ms,
                                        uint32_t hls_playlist_depth);

}  // namespace stream_hub_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_SRC_STREAM_HUB_STREAM_STATE_H_
