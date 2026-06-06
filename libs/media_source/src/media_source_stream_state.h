#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_MEDIA_SOURCE_STREAM_STATE_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_MEDIA_SOURCE_STREAM_STATE_H_

#include "media_source.h"

#include "flv_muxer.h"
#include "gop_cache.h"
#include "hls_maker.h"
#include "media/frame_attach.h"
#include "stream_codec.h"
#include "timestamp_corrector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace media_source_internal {

// 单路码流的浏览器播放状态。服务层只负责加锁和分发，HLS/FLV 的
// 参数集、分片缓存和打包游标都集中维护在这里。
struct StreamContext {
    VideoCodec codec = VideoCodec::kH264;
    StreamState state = StreamState::kClosed;
    std::string vps;
    std::string sps;
    std::string pps;
    std::string sequence_header_tag;
    GopCache flv_gop_cache;
    HlsMaker hls_maker;
    uint64_t config_generation = 0;
    TimestampCorrector timestamp_corrector;
};

using ParsedFramePayload = FramePayload;

struct PackagedFrameResult {
    bool accepted = false;
    bool keyframe = false;
    bool hls_segment_created = false;
    FlvVideoTagView flv_tag_view;
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
