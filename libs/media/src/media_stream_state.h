#ifndef LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_STATE_H_
#define LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_STATE_H_

#include "media/media_streams.h"

#include "flv_muxer.h"
#include "gop_cache.h"
#include "hls_maker.h"
#include "media/frame_sink.h"
#include "media_codec.h"
#include "media/timestamp_corrector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace media_internal {

// 单路码流的浏览器播放状态。服务层只负责加锁和分发，HLS/FLV 的
// 参数集、分片缓存和打包游标都集中维护在这里。
struct StreamContext {
    Codec codec = Codec::kH264;
    MediaStreamState state = MediaStreamState::kClosed;
    std::string vps;
    std::string sps;
    std::string pps;
    std::string sequence_header_tag;
    GopCache flv_gop_cache;
    HlsMaker hls_maker;
    uint64_t config_generation = 0;
    uint64_t codec_generation = 0;
    EncodedFrame latest_mjpeg_frame;
    bool has_latest_mjpeg_frame = false;
    MediaStreamResetReason last_reset_reason = MediaStreamResetReason::kNone;
    TimestampCorrector timestamp_corrector;
};

using ParsedFramePayload = FramePayload;

struct PackagedFrameResult {
    // accepted 只表示该帧被媒体源接收并完成必要封装；具体 HLS/FLV 是否产出
    // 数据由 hls_segment_created/has_flv_tag_view 分别表达。
    bool accepted = false;
    bool keyframe = false;
    bool hls_segment_created = false;
    FlvVideoTagView flv_tag_view;
    bool has_flv_tag_view = false;
};

struct NormalizedFrameResult {
    // timestamp_reset=true 时，上层需要同步重置 reader/GOP/HLS 等依赖时间连续性的缓存。
    bool accepted = false;
    bool timestamp_reset = false;
};

bool IsBrowserStreamReady(MediaStreamState state, Codec codec);
bool IsBrowserCodec(Codec codec);
bool IsFlvCodecSupported(Codec codec);
bool IsHlsCodecSupported(Codec codec);
bool IsMjpegCodecSupported(Codec codec);
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
MediaTrack BuildMediaTrack(StreamId stream_id, const StreamContext &stream);

void ResetStream(StreamContext *stream, Codec codec,
                 MediaStreamResetReason reason);
void ResetStreamCaches(StreamContext *stream, MediaStreamResetReason reason);
NormalizedFrameResult NormalizeFrameTimestamps(StreamContext *stream,
                                               EncodedFrame *frame);
bool StoreMjpegFrame(StreamContext *stream, const EncodedFrame &frame);
PackagedFrameResult AppendFrameToStream(StreamContext *stream,
                                        const EncodedFrame &frame,
                                        const ParsedFramePayload &payload,
                                        bool package_hls,
                                        bool package_flv,
                                        uint32_t hls_segment_duration_ms,
                                        uint32_t hls_playlist_depth);

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_STATE_H_
