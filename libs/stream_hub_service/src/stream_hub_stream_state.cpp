#include "stream_hub_stream_state.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace live_stream {
namespace stream_hub_internal {
bool IsBrowserCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

namespace {

void StartSegment(StreamContext *stream, int64_t pts_us) {
    if (stream == nullptr) {
        return;
    }
    stream->current_segment = HlsSegmentState{};
    stream->current_segment.started = true;
    stream->current_segment.published = false;
    stream->current_segment.sequence = stream->next_segment_sequence++;
    stream->current_segment.start_pts_us = pts_us;
    stream->current_segment.last_pts_us = pts_us;
    stream->current_segment.body =
        stream_mux::BuildTsSegmentHeader(stream->codec,
                                         &stream->ts_muxer_state);
}

StreamSegment BuildSegmentFromCurrent(const StreamContext &stream) {
    StreamSegment segment;
    if (!stream.current_segment.started || stream.current_segment.body.empty()) {
        return segment;
    }
    segment.found = true;
    segment.sequence = stream.current_segment.sequence;
    segment.duration_us =
        std::max<int64_t>(stream.last_frame_duration_us,
                          stream.current_segment.last_pts_us -
                              stream.current_segment.start_pts_us +
                              stream.last_frame_duration_us);
    segment.body = stream.current_segment.body;
    return segment;
}

void UpsertSegment(StreamContext *stream, const StreamSegment &segment,
                   uint32_t playlist_depth) {
    if (stream == nullptr || !segment.found || segment.body.empty()) {
        return;
    }
    for (StreamSegment &cached : stream->segments) {
        if (cached.sequence == segment.sequence) {
            cached.duration_us = segment.duration_us;
            cached.body = segment.body;
            return;
        }
    }
    stream->segments.push_back(segment);
    while (stream->segments.size() > playlist_depth) {
        stream->segments.pop_front();
    }
}

bool PublishCurrentSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.published) {
        return false;
    }
    const StreamSegment segment = BuildSegmentFromCurrent(*stream);
    if (!segment.found || segment.body.empty()) {
        return false;
    }
    UpsertSegment(stream, segment, playlist_depth);
    stream->current_segment.published = true;
    return true;
}

void UpdatePublishedCurrentSegment(StreamContext *stream,
                                   uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        !stream->current_segment.published) {
        return;
    }
    UpsertSegment(stream, BuildSegmentFromCurrent(*stream), playlist_depth);
}

bool FinalizeCurrentSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body.empty()) {
        return false;
    }

    UpsertSegment(stream, BuildSegmentFromCurrent(*stream), playlist_depth);
    stream->current_segment = HlsSegmentState{};
    return true;
}

void UpdateFrameTiming(StreamContext *stream, const EncodedFrame &frame) {
    if (stream == nullptr) {
        return;
    }
    if (stream->last_pts_us > 0 && frame.pts_us > stream->last_pts_us) {
        stream->last_frame_duration_us = frame.pts_us - stream->last_pts_us;
    }
    stream->last_pts_us = frame.pts_us;
}

std::string BuildH264Outputs(StreamContext *stream, const EncodedFrame &frame,
                             const ParsedFramePayload &payload,
                             bool package_hls, bool *keyframe,
                             std::string *access_unit) {
    bool has_sps = false;
    bool has_pps = false;
    stream_codec::ExtractH264ParameterSets(payload.h264_units, &stream->sps,
                                           &stream->pps, &has_sps, &has_pps);
    if (!stream->sps.empty() && !stream->pps.empty() && (has_sps || has_pps)) {
        stream->sequence_header_tag = stream_mux::BuildH264FlvSequenceHeaderTag(
            stream->sps, stream->pps, static_cast<uint32_t>(frame.dts_us / 1000));
        ++stream->config_generation;
    }

    *keyframe = *keyframe || stream_codec::HasH264KeyFrame(payload.h264_units);
    const bool frame_has_parameter_sets =
        stream_codec::HasH264ParameterSets(payload.h264_units);
    if (package_hls && access_unit != nullptr) {
        *access_unit = stream_codec::BuildH264AnnexBAccessUnit(
            payload.h264_units, stream->sps, stream->pps,
            *keyframe && !frame_has_parameter_sets);
    }
    return stream_codec::BuildH264AvccSample(payload.h264_units);
}

std::string BuildH265Outputs(StreamContext *stream, const EncodedFrame &frame,
                             const ParsedFramePayload &payload,
                             bool package_hls, bool *keyframe,
                             std::string *access_unit) {
    bool has_vps = false;
    bool has_sps = false;
    bool has_pps = false;
    stream_codec::ExtractH265ParameterSets(payload.h265_units, &stream->vps,
                                           &stream->sps, &stream->pps, &has_vps,
                                           &has_sps, &has_pps);
    if (!stream->vps.empty() && !stream->sps.empty() && !stream->pps.empty() &&
        (has_vps || has_sps || has_pps)) {
        stream->sequence_header_tag = stream_mux::BuildH265FlvSequenceHeaderTag(
            stream->vps, stream->sps, stream->pps,
            static_cast<uint32_t>(frame.dts_us / 1000));
        ++stream->config_generation;
    }

    *keyframe = *keyframe || stream_codec::HasH265KeyFrame(payload.h265_units);
    const bool frame_has_parameter_sets =
        stream_codec::HasH265ParameterSets(payload.h265_units);
    if (package_hls && access_unit != nullptr) {
        *access_unit = stream_codec::BuildH265AnnexBAccessUnit(
            payload.h265_units, stream->vps, stream->sps, stream->pps,
            *keyframe && !frame_has_parameter_sets);
    }
    return stream_codec::BuildH265LengthPrefixedSample(payload.h265_units);
}

std::string BuildFlvVideoTag(VideoCodec codec, bool keyframe,
                             int32_t composition_time_ms,
                             uint32_t timestamp_ms,
                             const std::string &length_prefixed_sample) {
    if (codec == VideoCodec::kH265) {
        return stream_mux::BuildH265FlvVideoTag(keyframe, composition_time_ms,
                                                timestamp_ms,
                                                length_prefixed_sample);
    }
    return stream_mux::BuildH264FlvVideoTag(keyframe, composition_time_ms,
                                            timestamp_ms,
                                            length_prefixed_sample);
}

}  // namespace

bool IsBrowserStreamReady(StreamState state, VideoCodec codec) {
    return state == StreamState::kRunning && IsBrowserCodec(codec);
}

bool IsFlvCodecSupported(VideoCodec codec) {
    return codec == VideoCodec::kH264;
}

bool IsHlsCodecSupported(VideoCodec codec) {
    return IsBrowserCodec(codec);
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
           !stream.segments.empty();
}

void ParseFramePayload(const EncodedFrame &frame, ParsedFramePayload *payload) {
    if (payload == nullptr) {
        return;
    }
    *payload = ParsedFramePayload{};
    payload->codec = frame.codec;
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr) {
        return;
    }

    if (frame.codec == VideoCodec::kH264) {
        payload->valid =
            stream_codec::ParseH264AnnexBNalUnits(data, frame.size,
                                                  &payload->h264_units);
    } else if (frame.codec == VideoCodec::kH265) {
        payload->valid =
            stream_codec::ParseH265AnnexBNalUnits(data, frame.size,
                                                  &payload->h265_units);
    }
}

bool HasParsedUnits(const ParsedFramePayload &payload) {
    if (!payload.valid) {
        return false;
    }
    if (payload.codec == VideoCodec::kH264) {
        return !payload.h264_units.empty();
    }
    if (payload.codec == VideoCodec::kH265) {
        return !payload.h265_units.empty();
    }
    return false;
}

StreamHlsPlaylist BuildHlsPlaylist(const StreamContext &stream,
                                   uint32_t hls_segment_duration_ms) {
    StreamHlsPlaylist playlist;
    if (!IsHlsStreamReady(stream)) {
        return playlist;
    }

    playlist.supported = true;
    playlist.media_sequence = stream.segments.front().sequence;
    int64_t max_duration_us = static_cast<int64_t>(hls_segment_duration_ms) * 1000;
    for (const StreamSegment &segment : stream.segments) {
        playlist.entries.push_back(
            StreamHlsEntry{segment.sequence, segment.duration_us});
        max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    playlist.target_duration_sec = static_cast<uint32_t>(
        std::max<int64_t>(1, (max_duration_us + 999999) / 1000000));
    return playlist;
}

StreamSegment FindHlsSegment(const StreamContext &stream, uint64_t sequence) {
    if (!IsBrowserStreamReady(stream.state, stream.codec)) {
        return StreamSegment{};
    }

    for (const StreamSegment &segment : stream.segments) {
        if (segment.sequence == sequence) {
            return segment;
        }
    }
    return StreamSegment{};
}

StreamFlvStartData BuildFlvStartData(const StreamContext &stream) {
    StreamFlvStartData start_data;
    if (!IsBrowserStreamReady(stream.state, stream.codec) ||
        !IsFlvCodecSupported(stream.codec)) {
        return start_data;
    }

    start_data.supported = true;
    start_data.file_header = stream_mux::BuildFlvFileHeader();
    start_data.sequence_header = stream.sequence_header_tag;
    start_data.last_keyframe = stream.last_keyframe_tag;
    start_data.config_generation = stream.config_generation;
    return start_data;
}

void ResetStream(StreamContext *stream, VideoCodec codec) {
    if (stream == nullptr) {
        return;
    }

    const StreamState state = stream->state;
    *stream = StreamContext{};
    stream->codec = codec;
    stream->state = state;
}

PackagedFrameResult AppendFrameToStream(StreamContext *stream,
                                        const EncodedFrame &frame,
                                        const ParsedFramePayload &payload,
                                        bool package_hls,
                                        uint32_t hls_segment_duration_ms,
                                        uint32_t hls_playlist_depth) {
    PackagedFrameResult result;
    if (stream == nullptr || frame.codec != payload.codec ||
        !HasParsedUnits(payload)) {
        return result;
    }
    if (stream->codec != frame.codec) {
        ResetStream(stream, frame.codec);
    }
    if (!IsBrowserStreamReady(stream->state, stream->codec)) {
        return result;
    }

    UpdateFrameTiming(stream, frame);

    // FLV always needs a length-prefixed sample; HLS only builds the Annex-B
    // access unit after an HLS client has requested this stream.
    bool keyframe = stream_codec::IsKeyFrame(frame.frame_type);
    std::string access_unit;
    const std::string length_prefixed_sample =
        frame.codec == VideoCodec::kH265
            ? BuildH265Outputs(stream, frame, payload, package_hls, &keyframe,
                               &access_unit)
            : BuildH264Outputs(stream, frame, payload, package_hls, &keyframe,
                               &access_unit);

    if (package_hls && keyframe && stream->current_segment.started &&
        frame.pts_us - stream->current_segment.start_pts_us >=
            static_cast<int64_t>(hls_segment_duration_ms) * 1000) {
        result.hls_segment_created =
            FinalizeCurrentSegment(stream, hls_playlist_depth);
    }
    if (package_hls && keyframe && !stream->current_segment.started) {
        StartSegment(stream, frame.pts_us);
    }
    if (package_hls && stream->current_segment.started) {
        stream_mux::AppendVideoAccessUnitToTsSegment(
            frame.codec, access_unit, frame.pts_us, frame.dts_us,
            &stream->ts_muxer_state, &stream->current_segment.body);
        stream->current_segment.last_pts_us = frame.pts_us;
        if (keyframe) {
            result.hls_segment_updated =
                PublishCurrentSegment(stream, hls_playlist_depth);
        } else {
            UpdatePublishedCurrentSegment(stream, hls_playlist_depth);
        }
    }

    if (!length_prefixed_sample.empty()) {
        const int64_t composition_time_ms = (frame.pts_us - frame.dts_us) / 1000;
        result.flv_tag = BuildFlvVideoTag(
            frame.codec, keyframe, static_cast<int32_t>(composition_time_ms),
            static_cast<uint32_t>(frame.dts_us / 1000), length_prefixed_sample);
        if (keyframe && !stream->sequence_header_tag.empty()) {
            stream->last_keyframe_tag = result.flv_tag;
        }
    }

    result.keyframe = keyframe;
    result.sequence_header_tag = stream->sequence_header_tag;
    result.accepted = true;
    return result;
}

}  // namespace stream_hub_internal
}  // namespace live_stream
