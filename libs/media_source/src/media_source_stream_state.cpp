#include "media_source_stream_state.h"

#include <cstdint>
#include <utility>

namespace live_stream {
namespace media_source_internal {
bool IsBrowserCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265 ||
           codec == VideoCodec::kMjpeg;
}

namespace {

void PushFlvGopCache(StreamContext *stream, const EncodedFrame &frame,
                     bool keyframe,
                     const media_mux::FlvVideoTagView &flv_tag_view) {
    if (stream == nullptr || stream->sequence_header_tag.empty()) {
        return;
    }
    (void)stream->flv_gop_cache.AppendFlvTag(frame, keyframe, flv_tag_view);
}

void BuildH264Outputs(StreamContext *stream, const EncodedFrame &frame,
                      const ParsedFramePayload &payload, bool *keyframe,
                      bool *prepend_parameter_sets) {
    bool has_sps = false;
    bool has_pps = false;
    media_codec::ExtractH264ParameterSets(payload.h264_units, &stream->sps,
                                           &stream->pps, &has_sps, &has_pps);
    if (!stream->sps.empty() && !stream->pps.empty() && (has_sps || has_pps)) {
        stream->sequence_header_tag = FlvMuxer::BuildSequenceHeader(
            VideoCodec::kH264, std::string(), stream->sps, stream->pps,
            static_cast<uint32_t>(frame.dts_us / 1000));
        ++stream->config_generation;
    }

    *keyframe = *keyframe || media_codec::HasH264KeyFrame(payload.h264_units);
    const bool frame_has_parameter_sets =
        media_codec::HasH264ParameterSets(payload.h264_units);
    if (prepend_parameter_sets != nullptr) {
        *prepend_parameter_sets = *keyframe && !frame_has_parameter_sets;
    }
}

void BuildH265Outputs(StreamContext *stream, const ParsedFramePayload &payload,
                      const EncodedFrame &frame, bool *keyframe,
                      bool *prepend_parameter_sets) {
    bool has_vps = false;
    bool has_sps = false;
    bool has_pps = false;
    media_codec::ExtractH265ParameterSets(payload.h265_units, &stream->vps,
                                           &stream->sps, &stream->pps, &has_vps,
                                           &has_sps, &has_pps);
    if (!stream->vps.empty() && !stream->sps.empty() && !stream->pps.empty() &&
        (has_vps || has_sps || has_pps)) {
        stream->sequence_header_tag = FlvMuxer::BuildSequenceHeader(
            VideoCodec::kH265, stream->vps, stream->sps, stream->pps,
            static_cast<uint32_t>(frame.dts_us / 1000));
        ++stream->config_generation;
    }

    *keyframe = *keyframe || media_codec::HasH265KeyFrame(payload.h265_units);
    const bool frame_has_parameter_sets =
        media_codec::HasH265ParameterSets(payload.h265_units);
    if (prepend_parameter_sets != nullptr) {
        *prepend_parameter_sets = *keyframe && !frame_has_parameter_sets;
    }
}

}  // namespace

bool IsBrowserStreamReady(StreamState state, VideoCodec codec) {
    return state == StreamState::kRunning && IsBrowserCodec(codec);
}

bool IsFlvCodecSupported(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

bool IsHlsCodecSupported(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

bool IsMjpegCodecSupported(VideoCodec codec) {
    return codec == VideoCodec::kMjpeg;
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
           IsMjpegCodecSupported(stream.codec);
}

void ParseFramePayload(const EncodedFrame &frame, ParsedFramePayload *payload) {
    if (payload == nullptr) {
        return;
    }
    FramePayloadUnref(payload);
    if (!EncodedFrameRefCopy(&payload->encoded_frame, &frame)) {
        return;
    }
    payload->has_nal_units = true;
    const uint8_t *data = EncodedFramePayloadData(&frame);
    if (data == nullptr) {
        payload->has_nal_units = false;
        return;
    }

    if (frame.codec == VideoCodec::kH264) {
        payload->has_nal_units =
            media_codec::ParseH264AnnexBNalUnits(data, frame.size,
                                                  &payload->h264_units);
    } else if (frame.codec == VideoCodec::kH265) {
        payload->has_nal_units =
            media_codec::ParseH265AnnexBNalUnits(data, frame.size,
                                                  &payload->h265_units);
    } else {
        payload->has_nal_units = false;
    }
}

bool HasParsedUnits(const ParsedFramePayload &payload) {
    if (!payload.has_nal_units) {
        return false;
    }
    if (payload.encoded_frame.codec == VideoCodec::kH264) {
        return !payload.h264_units.empty();
    }
    if (payload.encoded_frame.codec == VideoCodec::kH265) {
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
    stream->codec = VideoCodec::kH264;
    stream->state = StreamState::kClosed;
    stream->vps.clear();
    stream->sps.clear();
    stream->pps.clear();
    stream->sequence_header_tag.clear();
    stream->config_generation = 0;
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
        start_data.config_generation = stream.config_generation;
        return start_data;
    }
    stream.flv_gop_cache.CopyTo(&start_data);
    start_data.config_generation = stream.config_generation;
    return start_data;
}

void ResetStream(StreamContext *stream, VideoCodec codec) {
    if (stream == nullptr) {
        return;
    }

    const StreamState state = stream->state;
    ClearStreamContext(stream);
    stream->codec = codec;
    stream->state = state;
}

bool NormalizeFrameTimestamps(StreamContext *stream, EncodedFrame *frame) {
    if (stream == nullptr || frame == nullptr) {
        return false;
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

    const CorrectedTimestamp corrected =
        stream->timestamp_corrector.Correct(frame->dts_us, frame->pts_us);
    frame->dts_us = corrected.dts_us;
    frame->pts_us = corrected.pts_us;
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
        ResetStream(stream, frame.codec);
    }
    if (!IsBrowserStreamReady(stream->state, stream->codec)) {
        return result;
    }

    bool keyframe = media_codec::IsKeyFrame(frame.frame_type);
    bool prepend_parameter_sets = false;
    const uint64_t config_generation_before = stream->config_generation;
    if (frame.codec == VideoCodec::kH265) {
        BuildH265Outputs(stream, payload, frame, &keyframe,
                         &prepend_parameter_sets);
    } else {
        BuildH264Outputs(stream, frame, payload, &keyframe,
                         &prepend_parameter_sets);
    }
    if (stream->config_generation != config_generation_before) {
        stream->flv_gop_cache.Clear();
    }

    if (package_hls) {
        bool hls_segment_created = false;
        if (stream->hls_maker.AppendFrame(
                frame, payload, stream->vps, stream->sps, stream->pps,
                keyframe, prepend_parameter_sets, hls_segment_duration_ms,
                hls_playlist_depth, &hls_segment_created)) {
            result.hls_segment_created = hls_segment_created;
        }
    }

    if (package_flv && IsFlvCodecSupported(frame.codec)) {
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

}  // namespace media_source_internal
}  // namespace live_stream
