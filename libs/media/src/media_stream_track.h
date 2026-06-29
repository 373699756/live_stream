#ifndef LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_TRACK_H_
#define LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_TRACK_H_

#include "media/media_streams.h"

#include "flv_muxer.h"
#include "frame_payload.h"
#include "gop_cache.h"
#include "hls_maker.h"
#include "media/frame_sink.h"
#include "media_codec.h"
#include "timestamp_corrector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace media_internal {

// 单路码流的实时预览输出状态。服务层只负责加锁和分发，HLS/FLV 的
// 参数集、分片缓存和打包游标都集中维护在这里。
struct StreamTrack {
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
    MediaFrame latest_mjpeg_frame;
    bool has_latest_mjpeg_frame = false;
    MediaStreamResetReason last_reset_reason = MediaStreamResetReason::kNone;
    TimestampCorrector timestamp_corrector;
};

struct StreamTrackCacheOptions {
    GopCacheOptions flv_gop_cache;
    HlsMakerOptions hls_maker;
};

using ParsedFramePayload = FramePayload;

struct PackagedFrameResult {
    // accepted 只表示该帧被媒体源接收并完成必要封装；具体 HLS/FLV 是否产出
    // 数据由 hls_segment_created/has_flv_tag_view 分别表达。
    bool accepted = false;
    bool keyframe = false;
    bool hls_segment_created = false;
    bool has_flv_tag_view = false;
};

struct NormalizedFrameResult {
    // timestamp_reset=true 时，上层需要同步重置 subscription/GOP/HLS 等依赖时间连续性的缓存。
    bool accepted = false;
    bool timestamp_reset = false;
};

bool IsPreviewStreamReady(MediaStreamState state, Codec codec);
bool IsPreviewCodec(Codec codec);
bool IsFlvCodecSupported(Codec codec);
bool IsHlsCodecSupported(Codec codec);
bool IsMjpegCodecSupported(Codec codec);
bool IsFlvSequenceHeaderReady(const StreamTrack &stream);
bool IsFlvStreamReady(const StreamTrack &stream);
bool IsHlsStreamReady(const StreamTrack &stream);
bool IsMjpegStreamReady(const StreamTrack &stream);

void ParseFramePayload(const MediaFrame &frame, ParsedFramePayload &payload);
bool IsFramePayloadParsed(const ParsedFramePayload &payload);
void ClearStreamTrack(StreamTrack &stream);
void ConfigureStreamTrack(StreamTrack &stream,
                          const StreamTrackCacheOptions &options);

MediaHlsPlaylist BuildHlsPlaylist(const StreamTrack &stream,
                                  uint32_t hls_segment_duration_ms,
                                  uint32_t hls_playlist_depth);
MediaSegmentRef FindHlsSegmentRef(const StreamTrack &stream,
                                  uint64_t sequence);
MediaFlvStart BuildFlvStart(const StreamTrack &stream);
MediaStreamInfo BuildMediaStreamInfo(const StreamTrack &stream);

void ResetStream(StreamTrack &stream, Codec codec,
                 MediaStreamResetReason reason);
void ResetStreamCaches(StreamTrack &stream, MediaStreamResetReason reason);
NormalizedFrameResult NormalizeFrameTimestamps(StreamTrack &stream,
                                               MediaFrame &frame);
bool CacheMjpegFrame(StreamTrack &stream, const MediaFrame &frame);
PackagedFrameResult AppendFrameToStream(StreamTrack &stream,
                                        const MediaFrame &frame,
                                        const ParsedFramePayload &payload,
                                        bool package_hls,
                                        bool package_flv,
                                        uint32_t hls_segment_duration_ms,
                                        FlvVideoTagBuild &flv_tag_view);

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_TRACK_H_
