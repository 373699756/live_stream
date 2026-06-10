#include "media_stream_state.h"

#include <cstdint>
#include <utility>

namespace live_stream {
namespace media_internal {
bool IsBrowserCodec(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265 ||
           codec == Codec::kMjpeg;
}

namespace {

void PushFlvGopCache(StreamContext *stream, const EncodedFrame &frame,
                     bool keyframe,
                     const FlvVideoTagView &flv_tag_view) {
    if (stream == nullptr || stream->sequence_header_tag.empty()) {
        return;
    }
    (void)stream->flv_gop_cache.AppendFlvTag(frame, keyframe, flv_tag_view);
}

void BuildH264Outputs(StreamContext *stream, const EncodedFrame &frame,
                      const ParsedFramePayload &payload, bool *keyframe,
                      bool *prepend_parameter_sets) {
    // H.264 参数集既决定 FLV sequence header，也决定 WebRTC/RTSP track 是否 ready。
    // 参数集可能在 IDR 前重复出现，因此帧内携带 SPS/PPS 时重建输出配置。
    bool has_sps = false;
    bool has_pps = false;
    media_codec::ExtractH264ParameterSets(payload.h264_units, &stream->sps,
                                           &stream->pps, &has_sps, &has_pps);
    if (!stream->sps.empty() && !stream->pps.empty() && (has_sps || has_pps)) {
        stream->sequence_header_tag = FlvMuxer::BuildSequenceHeader(
            Codec::kH264, std::string(), stream->sps, stream->pps,
            static_cast<uint32_t>(frame.dts_us / 1000));
        // config_generation 只描述 FLV/codec 配置更新，不等同于 reader 的
        // codec_generation；FLV client 用它判断是否需要重发 sequence header。
        ++stream->config_generation;
    }

    *keyframe = *keyframe || media_codec::HasH264KeyFrame(payload.h264_units);
    const bool frame_has_parameter_sets =
        media_codec::HasH264ParameterSets(payload.h264_units);
    if (prepend_parameter_sets != nullptr) {
        // 当前帧是关键帧但不自带参数集时，HLS segment 需要前置缓存的 SPS/PPS，
        // 保证从 segment 边界接入的播放器也能解码。
        *prepend_parameter_sets = *keyframe && !frame_has_parameter_sets;
    }
}

void BuildH265Outputs(StreamContext *stream, const ParsedFramePayload &payload,
                      const EncodedFrame &frame, bool *keyframe,
                      bool *prepend_parameter_sets) {
    // H.265 ready 条件比 H.264 多 VPS。三类参数集齐全后才生成 enhanced FLV
    // sequence header，并允许浏览器协议链路进入 ready。
    bool has_vps = false;
    bool has_sps = false;
    bool has_pps = false;
    media_codec::ExtractH265ParameterSets(payload.h265_units, &stream->vps,
                                           &stream->sps, &stream->pps, &has_vps,
                                           &has_sps, &has_pps);
    if (!stream->vps.empty() && !stream->sps.empty() && !stream->pps.empty() &&
        (has_vps || has_sps || has_pps)) {
        stream->sequence_header_tag = FlvMuxer::BuildSequenceHeader(
            Codec::kH265, stream->vps, stream->sps, stream->pps,
            static_cast<uint32_t>(frame.dts_us / 1000));
        ++stream->config_generation;
    }

    *keyframe = *keyframe || media_codec::HasH265KeyFrame(payload.h265_units);
    const bool frame_has_parameter_sets =
        media_codec::HasH265ParameterSets(payload.h265_units);
    if (prepend_parameter_sets != nullptr) {
        // HEVC 关键帧前置参数集时必须一次带上 VPS/SPS/PPS。
        *prepend_parameter_sets = *keyframe && !frame_has_parameter_sets;
    }
}

}  // namespace

bool IsBrowserStreamReady(MediaStreamState state, Codec codec) {
    return state == MediaStreamState::kRunning && IsBrowserCodec(codec);
}

bool IsFlvCodecSupported(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

bool IsHlsCodecSupported(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

bool IsMjpegCodecSupported(Codec codec) {
    return codec == Codec::kMjpeg;
}

bool HasFlvSequenceHeader(const StreamContext &stream) {
    return !stream.sequence_header_tag.empty();
}

bool IsFlvStreamReady(const StreamContext &stream) {
    return IsBrowserStreamReady(stream.state, stream.codec) &&
           IsFlvCodecSupported(stream.codec) &&
           HasFlvSequenceHeader(stream);
}

bool IsHlsStreamReady(const StreamContext &stream) {
    return IsBrowserStreamReady(stream.state, stream.codec) &&
           IsHlsCodecSupported(stream.codec) &&
           stream.hls_maker.HasSegments();
}

bool IsMjpegStreamReady(const StreamContext &stream) {
    return IsBrowserStreamReady(stream.state, stream.codec) &&
           IsMjpegCodecSupported(stream.codec) &&
           stream.has_latest_mjpeg_frame;
}

void ParseFramePayload(const EncodedFrame &frame, ParsedFramePayload *payload) {
    if (payload == nullptr) {
        return;
    }
    FramePayloadUnref(payload);
    if (!EncodedFrameRefCopy(&payload->encoded_frame, &frame)) {
        return;
    }
    // FramePayload 持有原始 EncodedFrame 引用，NAL list 只是指向该 payload 的视图；
    // 不在解析阶段复制整帧视频数据。
    payload->has_nal_units = true;
    const uint8_t *data = EncodedFramePayloadData(&frame);
    if (data == nullptr) {
        payload->has_nal_units = false;
        return;
    }

    if (frame.codec == Codec::kH264) {
        payload->has_nal_units =
            media_codec::ParseH264AnnexBNalUnits(data, frame.payload.size,
                                                  &payload->h264_units);
    } else if (frame.codec == Codec::kH265) {
        payload->has_nal_units =
            media_codec::ParseH265AnnexBNalUnits(data, frame.payload.size,
                                                  &payload->h265_units);
    } else {
        payload->has_nal_units = false;
    }
}

bool HasParsedUnits(const ParsedFramePayload &payload) {
    if (!payload.has_nal_units) {
        return false;
    }
    if (payload.encoded_frame.codec == Codec::kH264) {
        return !payload.h264_units.empty();
    }
    if (payload.encoded_frame.codec == Codec::kH265) {
        return !payload.h265_units.empty();
    }
    return false;
}

void ParsedFramePayloadUnref(ParsedFramePayload *payload) {
    FramePayloadUnref(payload);
}

void ClearStreamContext(StreamContext *stream) {
    if (stream == nullptr) {
        return;
    }
    stream->flv_gop_cache.Clear();
    stream->hls_maker.Reset();
    EncodedFrameUnref(&stream->latest_mjpeg_frame);
    stream->codec = Codec::kH264;
    stream->state = MediaStreamState::kClosed;
    stream->vps.clear();
    stream->sps.clear();
    stream->pps.clear();
    stream->sequence_header_tag.clear();
    stream->config_generation = 0;
    stream->codec_generation = 0;
    stream->has_latest_mjpeg_frame = false;
    stream->last_reset_reason = MediaStreamResetReason::kStreamStopped;
    stream->timestamp_corrector.Reset();
}

MediaHlsPlaylist BuildHlsPlaylist(const StreamContext &stream,
                                   uint32_t hls_segment_duration_ms,
                                   uint32_t hls_playlist_depth) {
    MediaHlsPlaylist playlist;
    if (!IsHlsStreamReady(stream)) {
        return playlist;
    }

    return stream.hls_maker.BuildPlaylist(hls_segment_duration_ms,
                                          hls_playlist_depth);
}

MediaSegmentRef FindHlsSegmentRef(const StreamContext &stream,
                                   uint64_t sequence) {
    if (!IsBrowserStreamReady(stream.state, stream.codec)) {
        return MediaSegmentRef{};
    }

    return stream.hls_maker.FindSegmentRef(sequence);
}

MediaFlvStartData BuildFlvStartData(const StreamContext &stream) {
    MediaFlvStartData start_data;
    if (!IsBrowserStreamReady(stream.state, stream.codec) ||
        !IsFlvCodecSupported(stream.codec)) {
        return start_data;
    }

    start_data.supported = true;
    start_data.cached_gop_complete = stream.flv_gop_cache.complete();
    start_data.file_header = FlvMuxer::BuildFileHeader();
    start_data.sequence_header = stream.sequence_header_tag;
    if (!stream.flv_gop_cache.complete()) {
        // sequence header 已可发送但还没有完整 GOP 时，新客户端需要等待 live
        // 关键帧，不能从缓存 P 帧起播。
        start_data.config_generation = stream.config_generation;
        return start_data;
    }
    stream.flv_gop_cache.CopyTo(&start_data);
    start_data.config_generation = stream.config_generation;
    return start_data;
}

MediaTrack BuildMediaTrack(StreamId stream_id, const StreamContext &stream) {
    MediaTrack track;
    track.track_type = MediaTrackType::kVideo;
    track.stream_id = stream_id;
    track.codec = stream.codec;
    track.clock_rate = 90000;
    track.codec_generation = stream.codec_generation;
    track.vps = stream.vps;
    track.sps = stream.sps;
    track.pps = stream.pps;
    // track_ready 面向 RTSP/WebRTC/HTTP-FLV 等协议输出。H.264/H.265 必须已有
    // sequence header；MJPEG 则至少要有一帧最新 JPEG。
    track.ready =
        IsBrowserStreamReady(stream.state, stream.codec) &&
        ((IsFlvCodecSupported(stream.codec) && HasFlvSequenceHeader(stream)) ||
         IsMjpegStreamReady(stream));
    return track;
}

void ResetStreamCaches(StreamContext *stream, MediaStreamResetReason reason) {
    if (stream == nullptr) {
        return;
    }
    stream->flv_gop_cache.Clear();
    stream->hls_maker.Reset();
    EncodedFrameUnref(&stream->latest_mjpeg_frame);
    stream->has_latest_mjpeg_frame = false;
    stream->vps.clear();
    stream->sps.clear();
    stream->pps.clear();
    stream->sequence_header_tag.clear();
    stream->config_generation = 0;
    // codec_generation 表示所有依赖 codec/时间连续性的缓存都进入新代际。
    // reader 用它判断旧 GOP/start data 是否仍可用。
    ++stream->codec_generation;
    stream->last_reset_reason = reason;
}

void ResetStream(StreamContext *stream, Codec codec,
                 MediaStreamResetReason reason) {
    if (stream == nullptr) {
        return;
    }
    const MediaStreamState state = stream->state;
    ResetStreamCaches(stream, reason);
    stream->timestamp_corrector.Reset();
    stream->codec = codec;
    stream->state = state;
}

NormalizedFrameResult NormalizeFrameTimestamps(StreamContext *stream,
                                               EncodedFrame *frame) {
    NormalizedFrameResult result;
    if (stream == nullptr || frame == nullptr) {
        return result;
    }

    if (frame->dts_us <= 0) {
        frame->dts_us = frame->pts_us;
    }
    if (frame->pts_us <= 0) {
        frame->pts_us = frame->dts_us;
    }
    if (frame->dts_us < 0 || frame->pts_us < 0) {
        frame->dts_us = 0;
        frame->pts_us = 0;
    }

    const TimestampCorrectionResult corrected =
        stream->timestamp_corrector.CorrectWithReset(frame->dts_us,
                                                     frame->pts_us);
    if (corrected.reset != TimestampCorrectionReset::kNone) {
        // 时间戳回退或大跳变会破坏 HLS segment 时长、GOP cache 和 reader
        // 起播点，必须统一清理后从后续关键帧恢复。
        ResetStreamCaches(stream, MediaStreamResetReason::kTimestampReset);
        result.timestamp_reset = true;
    }
    frame->dts_us = corrected.timestamp.dts_us;
    frame->pts_us = corrected.timestamp.pts_us;
    result.accepted = true;
    return result;
}

bool StoreMjpegFrame(StreamContext *stream, const EncodedFrame &frame) {
    if (stream == nullptr || frame.codec != Codec::kMjpeg ||
        !EncodedFrameHasPayload(&frame)) {
        return false;
    }
    EncodedFrame retained_frame;
    // MJPEG latest frame 只保存 FrameBuffer 引用；HTTP-MJPEG 发送时通过
    // HttpMediaSlice.owner 继续把同一块 payload 持有到网络发送完成。
    if (!EncodedFrameRefCopy(&retained_frame, &frame)) {
        return false;
    }
    EncodedFrameUnref(&stream->latest_mjpeg_frame);
    stream->latest_mjpeg_frame = retained_frame;
    stream->has_latest_mjpeg_frame = true;
    return true;
}

PackagedFrameResult AppendFrameToStream(StreamContext *stream,
                                        const EncodedFrame &frame,
                                        const ParsedFramePayload &payload,
                                        bool package_hls,
                                        bool package_flv,
                                        uint32_t hls_segment_duration_ms,
                                        uint32_t hls_playlist_depth) {
    PackagedFrameResult result;
    if (stream == nullptr || frame.codec != payload.encoded_frame.codec ||
        !HasParsedUnits(payload)) {
        return result;
    }
    if (stream->codec != frame.codec) {
        // codec 切换会让旧参数集、sequence header、HLS 当前 segment 和 GOP
        // 都失效，不能只改 codec 字段继续复用旧缓存。
        ResetStream(stream, frame.codec, MediaStreamResetReason::kCodecChanged);
    }
    if (!IsBrowserStreamReady(stream->state, stream->codec)) {
        return result;
    }

    bool keyframe = media_codec::IsKeyFrame(frame.frame_type);
    bool prepend_parameter_sets = false;
    const uint64_t config_generation_before = stream->config_generation;
    if (frame.codec == Codec::kH265) {
        BuildH265Outputs(stream, payload, frame, &keyframe,
                         &prepend_parameter_sets);
    } else {
        BuildH264Outputs(stream, frame, payload, &keyframe,
                         &prepend_parameter_sets);
    }
    if (stream->config_generation != config_generation_before) {
        // sequence header 更新后，旧 FLV GOP 对应旧配置，必须清空等待新关键帧。
        stream->flv_gop_cache.Clear();
    }

    if (package_hls) {
        bool hls_segment_created = false;
        // HLS 是转封装输出，会把输入 NAL 复制成独立 TS segment body。
        // FLV/WebRTC/RTSP 路径仍使用原 EncodedFrame 引用或 slice view。
        if (stream->hls_maker.AppendFrame(
                frame, payload, stream->vps, stream->sps, stream->pps,
                keyframe, prepend_parameter_sets, hls_segment_duration_ms,
                hls_playlist_depth, &hls_segment_created)) {
            result.hls_segment_created = hls_segment_created;
        }
    }

    if (package_flv && IsFlvCodecSupported(frame.codec)) {
        // FLV tag view 只生成小 header 和 length prefix；媒体 NAL payload
        // 仍指向 payload.encoded_frame 的 FrameBuffer。
        result.has_flv_tag_view = FlvMuxer::BuildVideoTagView(
            frame, payload, keyframe, &result.flv_tag_view);
        if (result.has_flv_tag_view) {
            PushFlvGopCache(stream, frame, keyframe, result.flv_tag_view);
        }
    }

    result.keyframe = keyframe;
    result.accepted = true;
    return result;
}

}  // namespace media_internal
}  // namespace live_stream
