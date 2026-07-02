#include "media_stream_track.h"

#include <cstdint>
#include <utility>

namespace live_stream {
namespace media_internal {
bool IsPreviewCodec(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265 ||
           codec == Codec::kMjpeg;
}

namespace {

void PushFlvGopCache(StreamTrack &stream, const MediaFrame &frame,
                     bool keyframe,
                     const FlvVideoTagBuild &flv_tag_view) {
    if (stream.sequence_header_tag.empty()) {
        return;
    }
    (void)stream.flv_gop_cache.AppendFlvTag(frame, keyframe, flv_tag_view);
}

void BuildH264Outputs(StreamTrack &stream, const MediaFrame &frame,
                      const ParsedFramePayload &payload, bool &keyframe,
                      bool &prepend_parameter_sets) {
    // H.264 参数集既决定 FLV sequence header，也决定 WebRTC/RTSP track 是否 ready。
    // 参数集可能在 IDR 前重复出现，因此帧内携带 SPS/PPS 时重建输出配置。
    bool has_sps = false;
    bool has_pps = false;
    media_codec::ExtractH264ParameterSets(payload.h264_units, &stream.sps,
                                          &stream.pps, &has_sps, &has_pps);
    if (!stream.sps.empty() && !stream.pps.empty() && (has_sps || has_pps)) {
        stream.sequence_header_tag = FlvMuxer::BuildSequenceHeader(
            Codec::kH264, std::string(), stream.sps, stream.pps,
            static_cast<uint32_t>(frame.dts_us / 1000));
        // config_generation 只描述 FLV/codec 配置更新，不等同于 subscription 的
        // codec_generation；FLV client 用它判断是否需要重发 sequence header。
        ++stream.config_generation;
    }

    keyframe = keyframe || media_codec::ContainsH264Keyframe(payload.h264_units);
    const bool frame_has_parameter_sets =
        media_codec::ContainsH264ParameterSets(payload.h264_units);
    // 当前帧是关键帧但不自带参数集时，HLS segment 需要前置缓存的 SPS/PPS，
    // 保证从 segment 边界接入的播放器也能解码。
    prepend_parameter_sets = keyframe && !frame_has_parameter_sets;
}

void BuildH265Outputs(StreamTrack &stream, const ParsedFramePayload &payload,
                      const MediaFrame &frame, bool &keyframe,
                      bool &prepend_parameter_sets) {
    (void)frame;
    // H.265 ready 条件比 H.264 多 VPS。三类参数集齐全后才允许 HLS fMP4、
    // WebRTC 和 RTSP 进入可输出状态。
    bool has_vps = false;
    bool has_sps = false;
    bool has_pps = false;
    media_codec::ExtractH265ParameterSets(payload.h265_units, &stream.vps,
                                          &stream.sps, &stream.pps, &has_vps,
                                          &has_sps, &has_pps);
    keyframe = keyframe || media_codec::ContainsH265Keyframe(payload.h265_units);
    const bool frame_has_parameter_sets =
        media_codec::ContainsH265ParameterSets(payload.h265_units);
    // HEVC 关键帧前置参数集时必须一次带上 VPS/SPS/PPS。
    prepend_parameter_sets = keyframe && !frame_has_parameter_sets;
}

}  // namespace

bool IsPreviewStreamReady(MediaStreamState state, Codec codec) {
    return state == MediaStreamState::kRunning && IsPreviewCodec(codec);
}

bool IsFlvCodecSupported(Codec codec) {
    // 当前浏览器侧播放链路使用 flv.js，不支持 H.265 FLV。
    // 为避免 codec_id 错误导致的握手/解码失败，HTTP-FLV 只承载 H.264。
    return codec == Codec::kH264;
}

bool IsHlsCodecSupported(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

bool IsMjpegCodecSupported(Codec codec) {
    return codec == Codec::kMjpeg;
}

bool IsFlvSequenceHeaderReady(const StreamTrack &stream) {
    return !stream.sequence_header_tag.empty();
}

bool IsFlvStreamReady(const StreamTrack &stream) {
    return IsPreviewStreamReady(stream.state, stream.codec) &&
           IsFlvCodecSupported(stream.codec) &&
           IsFlvSequenceHeaderReady(stream);
}

bool IsHlsStreamReady(const StreamTrack &stream) {
    return IsPreviewStreamReady(stream.state, stream.codec) &&
           IsHlsCodecSupported(stream.codec) &&
           stream.hls_maker.IsPlaylistReady();
}

bool IsMjpegStreamReady(const StreamTrack &stream) {
    return IsPreviewStreamReady(stream.state, stream.codec) &&
           IsMjpegCodecSupported(stream.codec) &&
           stream.has_latest_mjpeg_frame;
}

bool IsTrackReady(const StreamTrack &stream) {
    if (stream.codec == Codec::kH265) {
        return IsPreviewStreamReady(stream.state, stream.codec) &&
               !stream.vps.empty() && !stream.sps.empty() &&
               !stream.pps.empty();
    }
    // H.264 依赖 FLV sequence header 对应的 SPS/PPS；MJPEG 至少要有一帧。
    return IsPreviewStreamReady(stream.state, stream.codec) &&
           (IsFlvSequenceHeaderReady(stream) || IsMjpegStreamReady(stream));
}

void ParseFramePayload(const MediaFrame &frame, ParsedFramePayload &payload) {
    payload = ParsedFramePayload{};
    payload.frame = frame;
    // FramePayload 持有原始 MediaFrame 引用，NAL list 只是指向该 payload 的视图；
    // 不在解析阶段复制整帧视频数据。
    payload.has_nal_units = true;
    const uint8_t *data = MediaFramePayloadData(frame);
    if (data == nullptr) {
        payload.has_nal_units = false;
        return;
    }

    if (frame.codec == Codec::kH264) {
        payload.has_nal_units =
            media_codec::ParseH264AnnexBNalUnits(data, frame.payload.Size(),
                                                 &payload.h264_units);
    } else if (frame.codec == Codec::kH265) {
        payload.has_nal_units =
            media_codec::ParseH265AnnexBNalUnits(data, frame.payload.Size(),
                                                 &payload.h265_units);
    } else {
        payload.has_nal_units = false;
    }
}

bool IsFramePayloadParsed(const ParsedFramePayload &payload) {
    if (!payload.has_nal_units) {
        return false;
    }
    if (payload.frame.codec == Codec::kH264) {
        return !payload.h264_units.empty();
    }
    if (payload.frame.codec == Codec::kH265) {
        return !payload.h265_units.empty();
    }
    return false;
}

void ClearStreamTrack(StreamTrack &stream) {
    stream.flv_gop_cache.Clear();
    stream.hls_maker.Reset();
    stream.latest_mjpeg_frame = MediaFrame{};
    stream.codec = Codec::kH264;
    stream.state = MediaStreamState::kClosed;
    stream.vps.clear();
    stream.sps.clear();
    stream.pps.clear();
    stream.sequence_header_tag.clear();
    stream.config_generation = 0;
    stream.codec_generation = 0;
    stream.has_latest_mjpeg_frame = false;
    stream.last_reset_reason = MediaStreamResetReason::kStreamStopped;
    stream.timestamp_corrector.Reset();
}

void ConfigureStreamTrack(StreamTrack &stream,
                          const StreamTrackCacheOptions &options) {
    stream.flv_gop_cache.Configure(options.flv_gop_cache);
    stream.hls_maker.Configure(options.hls_maker);
}

MediaHlsPlaylist BuildHlsPlaylist(const StreamTrack &stream,
                                  uint32_t hls_segment_duration_ms,
                                  uint32_t hls_playlist_depth) {
    MediaHlsPlaylist playlist;
    if (!IsHlsStreamReady(stream)) {
        return playlist;
    }

    return stream.hls_maker.BuildPlaylist(hls_segment_duration_ms,
                                          hls_playlist_depth);
}

MediaSegmentRef FindHlsSegmentRef(const StreamTrack &stream,
                                  uint64_t sequence) {
    if (!IsPreviewStreamReady(stream.state, stream.codec)) {
        return MediaSegmentRef{};
    }

    MediaSegmentRef segment = stream.hls_maker.FindSegmentRef(sequence);
    if (!segment.found) {
        stream.hls_maker.RecordSegmentMiss();
    }
    return segment;
}

MediaFlvStart BuildFlvStart(const StreamTrack &stream) {
    MediaFlvStart flv_start;
    if (!IsPreviewStreamReady(stream.state, stream.codec) ||
        !IsFlvCodecSupported(stream.codec)) {
        return flv_start;
    }

    flv_start.supported = true;
    flv_start.cached_gop_complete = stream.flv_gop_cache.complete();
    flv_start.file_header = FlvMuxer::BuildFileHeader();
    flv_start.sequence_header = stream.sequence_header_tag;
    if (!stream.flv_gop_cache.complete()) {
        // sequence header 已可发送但还没有完整 GOP 时，新客户端需要等待 live
        // 关键帧，不能从缓存 P 帧起播。
        flv_start.config_generation = stream.config_generation;
        return flv_start;
    }
    stream.flv_gop_cache.CopyTo(flv_start);
    flv_start.config_generation = stream.config_generation;
    return flv_start;
}

MediaStreamInfo BuildMediaStreamInfo(const StreamTrack &stream) {
    MediaStreamInfo info;
    info.running = stream.state == MediaStreamState::kRunning;
    info.hls_supported = IsHlsCodecSupported(stream.codec);
    info.flv_supported = IsFlvCodecSupported(stream.codec);
    info.mjpeg_supported = IsMjpegCodecSupported(stream.codec);
    info.preview_codec =
        info.hls_supported || info.flv_supported || info.mjpeg_supported;
    // track_ready 面向 RTSP/WebRTC/HTTP-FLV 等协议输出。H.264/H.265 必须已有
    // sequence header；MJPEG 则至少要有一帧最新 JPEG。
    info.track_ready = IsTrackReady(stream);
    info.hls_ready = IsHlsStreamReady(stream);
    info.flv_ready = IsFlvStreamReady(stream);
    info.mjpeg_ready = IsMjpegStreamReady(stream);
    info.codec = stream.codec;
    info.clock_rate = 90000;
    info.codec_generation = stream.codec_generation;
    info.vps = stream.vps;
    info.sps = stream.sps;
    info.pps = stream.pps;
    info.hls_codec = stream.hls_maker.CodecString();
    info.hls_segment_size =
        static_cast<uint32_t>(stream.hls_maker.SegmentSize());
    info.hls_first_segment_sequence = stream.hls_maker.FirstSegmentSequence();
    info.hls_last_segment_sequence = stream.hls_maker.LastSegmentSequence();
    info.hls_missing_segments = stream.hls_maker.MissingSegments();
    info.hls_evicted_segments = stream.hls_maker.EvictedSegments();
    info.flv_sequence_header_size =
        static_cast<uint32_t>(stream.sequence_header_tag.size());
    info.flv_last_keyframe_size = stream.flv_gop_cache.FirstFlvTagSize();
    info.hls_current_segment_size = stream.hls_maker.CurrentSegmentSize();
    info.last_dts_us = stream.timestamp_corrector.last_dts_us();
    info.last_reset_reason = MediaStreamResetReasonName(stream.last_reset_reason);
    return info;
}

void ResetStreamCaches(StreamTrack &stream, MediaStreamResetReason reason) {
    stream.flv_gop_cache.Clear();
    stream.hls_maker.Reset();
    stream.latest_mjpeg_frame = MediaFrame{};
    stream.has_latest_mjpeg_frame = false;
    stream.vps.clear();
    stream.sps.clear();
    stream.pps.clear();
    stream.sequence_header_tag.clear();
    stream.config_generation = 0;
    // codec_generation 表示所有依赖 codec/时间连续性的缓存都进入新代际。
    // subscription 用它判断旧 GOP/start data 是否仍可用。
    ++stream.codec_generation;
    stream.last_reset_reason = reason;
}

void ResetStream(StreamTrack &stream, Codec codec,
                 MediaStreamResetReason reason) {
    const MediaStreamState state = stream.state;
    ResetStreamCaches(stream, reason);
    stream.timestamp_corrector.Reset();
    stream.codec = codec;
    stream.state = state;
}

NormalizedFrameResult NormalizeFrameTimestamps(StreamTrack &stream,
                                               MediaFrame &frame) {
    NormalizedFrameResult result;
    if (frame.dts_us <= 0) {
        frame.dts_us = frame.pts_us;
    }
    if (frame.pts_us <= 0) {
        frame.pts_us = frame.dts_us;
    }
    if (frame.dts_us < 0 || frame.pts_us < 0) {
        frame.dts_us = 0;
        frame.pts_us = 0;
    }

    const TimestampCorrectionResult corrected =
        stream.timestamp_corrector.CorrectWithReset(frame.dts_us,
                                                    frame.pts_us);
    if (corrected.reset != TimestampCorrectionReset::kNone) {
        // 时间戳回退或大跳变会破坏 HLS segment 时长、GOP cache 和 subscription
        // 起播点，必须统一清理后从后续关键帧恢复。
        ResetStreamCaches(stream, MediaStreamResetReason::kTimestampReset);
        result.timestamp_reset = true;
    }
    frame.dts_us = corrected.timestamp.dts_us;
    frame.pts_us = corrected.timestamp.pts_us;
    result.accepted = true;
    return result;
}

bool CacheMjpegFrame(StreamTrack &stream, const MediaFrame &frame) {
    if (frame.codec != Codec::kMjpeg || !IsMediaFramePayloadValid(frame)) {
        return false;
    }
    // MJPEG latest frame 只保存 MediaBuffer 引用；HTTP-MJPEG 发送时通过
    // MediaOutSlice.buffer 继续把同一块 payload 持有到网络发送完成。
    stream.latest_mjpeg_frame = frame;
    stream.has_latest_mjpeg_frame = true;
    return true;
}

PackagedFrameResult AppendFrameToStream(StreamTrack &stream,
                                        const MediaFrame &frame,
                                        const ParsedFramePayload &payload,
                                        bool package_hls,
                                        bool package_flv,
                                        uint32_t hls_segment_duration_ms,
                                        FlvVideoTagBuild &flv_tag_view) {
    PackagedFrameResult result;
    if (frame.codec != payload.frame.codec || !IsFramePayloadParsed(payload)) {
        return result;
    }
    if (stream.codec != frame.codec) {
        // codec 切换会让旧参数集、sequence header、HLS 当前 segment 和 GOP
        // 都失效，不能只改 codec 字段继续复用旧缓存。
        ResetStream(stream, frame.codec, MediaStreamResetReason::kCodecChanged);
    }
    if (!IsPreviewStreamReady(stream.state, stream.codec)) {
        return result;
    }

    bool keyframe = frame.frame_type == FrameType::kIdr ||
                    frame.frame_type == FrameType::kI;
    bool prepend_parameter_sets = false;
    const uint64_t config_generation_before = stream.config_generation;
    if (frame.codec == Codec::kH265) {
        BuildH265Outputs(stream, payload, frame, keyframe,
                         prepend_parameter_sets);
    } else {
        BuildH264Outputs(stream, frame, payload, keyframe,
                         prepend_parameter_sets);
    }
    if (stream.config_generation != config_generation_before) {
        // sequence header 更新后，旧 FLV GOP 对应旧配置，必须清空等待新关键帧。
        stream.flv_gop_cache.Clear();
    }

    if (package_hls) {
        bool hls_segment_created = false;
        // HLS 是转封装输出，会把输入 NAL 复制成独立 TS segment body。
        // FLV/WebRTC/RTSP 路径仍使用原 MediaFrame 引用或 slice view。
        if (stream.hls_maker.AppendFrame(
                frame, payload, stream.vps, stream.sps, stream.pps,
                keyframe, prepend_parameter_sets, hls_segment_duration_ms,
                hls_segment_created)) {
            result.hls_segment_created = hls_segment_created;
        }
    }

    if (package_flv && IsFlvCodecSupported(frame.codec)) {
        // FLV tag view 只生成小 header 和 length prefix；媒体 NAL payload
        // 仍指向 payload.frame 的 MediaBuffer。
        result.has_flv_tag_view =
            FlvMuxer::BuildVideoTagView(frame, payload, keyframe,
                                        &flv_tag_view);
        if (result.has_flv_tag_view) {
            PushFlvGopCache(stream, frame, keyframe, flv_tag_view);
        }
    }

    result.keyframe = keyframe;
    result.accepted = true;
    return result;
}

}  // namespace media_internal
}  // namespace live_stream
