#include "stream_hub_stream_state.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace live_stream {
namespace stream_hub_internal {
bool IsBrowserCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265 ||
           codec == VideoCodec::kMjpeg;
}

namespace {

constexpr size_t kInitialHlsSegmentBytes = 256 * 1024;

void ClearFlvGopCache(StreamContext *stream) {
    if (stream == nullptr) {
        return;
    }
    stream->flv_gop_cache = CachedFlvFrameRing{};
}

bool CopyFlvTagViewForCache(const EncodedFrame &frame,
                            const stream_mux::FlvVideoTagView &source,
                            StreamFlvCachedVideoTag *target) {
    if (target == nullptr || !frame.HasValidPayload() ||
        source.slice_count == 0 ||
        source.slice_count > kMaxStreamFlvVideoTagSlices) {
        return false;
    }

    StreamFlvCachedVideoTag cached_tag;
    cached_tag.frame = frame;
    cached_tag.slice_count = source.slice_count;
    cached_tag.total_size = source.total_size;
    cached_tag.timestamp_ms = source.timestamp_ms;
    for (size_t i = 0; i < source.slice_count; ++i) {
        const stream_mux::FlvVideoTagSlice &source_slice = source.slices[i];
        StreamFlvCachedVideoTagSlice &target_slice = cached_tag.slices[i];
        if (source_slice.data == nullptr || source_slice.size == 0) {
            return false;
        }
        if (source_slice.media_payload) {
            const uint8_t *payload = frame.PayloadData();
            const uintptr_t payload_addr =
                reinterpret_cast<uintptr_t>(payload);
            const uintptr_t source_addr =
                reinterpret_cast<uintptr_t>(source_slice.data);
            if (payload == nullptr || source_addr < payload_addr ||
                source_addr - payload_addr > frame.size ||
                source_slice.size > frame.size - (source_addr - payload_addr)) {
                return false;
            }
            target_slice.media_data = source_slice.data;
            target_slice.size = source_slice.size;
            target_slice.media_payload = true;
        } else {
            if (source_slice.size > sizeof(target_slice.header_data)) {
                return false;
            }
            std::copy(source_slice.data, source_slice.data + source_slice.size,
                      target_slice.header_data);
            target_slice.size = source_slice.size;
            target_slice.media_payload = false;
        }
    }

    *target = cached_tag;
    return true;
}

void PushFlvGopCache(StreamContext *stream, const EncodedFrame &frame,
                     bool keyframe,
                     const stream_mux::FlvVideoTagView &flv_tag_view) {
    if (stream == nullptr || stream->sequence_header_tag.empty()) {
        return;
    }
    if (keyframe) {
        ClearFlvGopCache(stream);
        stream->flv_gop_cache.complete = true;
    }
    if (stream->flv_gop_cache.size == 0 && !keyframe) {
        return;
    }
    if (stream->flv_gop_cache.size >= stream->flv_gop_cache.frames.size()) {
        ClearFlvGopCache(stream);
        return;
    }
    const size_t index =
        (stream->flv_gop_cache.head + stream->flv_gop_cache.size) %
        stream->flv_gop_cache.frames.size();
    if (!CopyFlvTagViewForCache(frame, flv_tag_view,
                                &stream->flv_gop_cache.frames[index])) {
        ClearFlvGopCache(stream);
        return;
    }
    ++stream->flv_gop_cache.size;
}

int64_t CurrentSegmentDurationUs(const StreamContext &stream) {
    return std::max<int64_t>(stream.last_frame_duration_us,
                             stream.current_segment.last_pts_us -
                                 stream.current_segment.start_pts_us +
                                 stream.last_frame_duration_us);
}

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
    stream->current_segment.body.reserve(kInitialHlsSegmentBytes);
}

StreamSegment BuildSegmentFromCurrent(const StreamContext &stream) {
    StreamSegment segment;
    if (!stream.current_segment.started || stream.current_segment.body.empty()) {
        return segment;
    }
    segment.found = true;
    segment.sequence = stream.current_segment.sequence;
    segment.duration_us = CurrentSegmentDurationUs(stream);
    segment.body = stream.current_segment.body;
    return segment;
}

void PushFinalizedSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body.empty()) {
        return;
    }
    stream->segments.push_back(StreamSegment{});
    StreamSegment &segment = stream->segments.back();
    segment.found = true;
    segment.sequence = stream->current_segment.sequence;
    segment.duration_us = CurrentSegmentDurationUs(*stream);
    segment.body.swap(stream->current_segment.body);
    while (stream->segments.size() > playlist_depth) {
        stream->segments.pop_front();
    }
}

void TrimSegments(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr) {
        return;
    }
    while (stream->segments.size() > playlist_depth) {
        stream->segments.pop_front();
    }
}

void TrimSegmentsWithCurrent(StreamContext *stream, uint32_t playlist_depth) {
    if (playlist_depth == 0) {
        TrimSegments(stream, 0);
        return;
    }
    TrimSegments(stream, playlist_depth - 1);
}

bool PublishCurrentSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.published ||
        stream->current_segment.body.empty()) {
        return false;
    }
    stream->current_segment.published = true;
    TrimSegmentsWithCurrent(stream, playlist_depth);
    return true;
}

bool FinalizeCurrentSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body.empty()) {
        return false;
    }

    PushFinalizedSegment(stream, playlist_depth);
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

void BuildH264Outputs(StreamContext *stream, const EncodedFrame &frame,
                      const ParsedFramePayload &payload, bool *keyframe,
                      bool *prepend_parameter_sets) {
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
           (!stream.segments.empty() || stream.current_segment.published);
}

bool IsMjpegStreamReady(const StreamContext &stream) {
    return IsBrowserStreamReady(stream.state, stream.codec) &&
           IsMjpegCodecSupported(stream.codec);
}

void ParseFramePayload(const EncodedFrame &frame, ParsedFramePayload *payload) {
    if (payload == nullptr) {
        return;
    }
    *payload = ParsedFramePayload{};
    payload->encoded_frame = frame;
    payload->has_nal_units = true;
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr) {
        payload->has_nal_units = false;
        return;
    }

    if (frame.codec == VideoCodec::kH264) {
        payload->has_nal_units =
            stream_codec::ParseH264AnnexBNalUnits(data, frame.size,
                                                  &payload->h264_units);
    } else if (frame.codec == VideoCodec::kH265) {
        payload->has_nal_units =
            stream_codec::ParseH265AnnexBNalUnits(data, frame.size,
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

StreamHlsPlaylist BuildHlsPlaylist(const StreamContext &stream,
                                   uint32_t hls_segment_duration_ms) {
    StreamHlsPlaylist playlist;
    if (!IsHlsStreamReady(stream)) {
        return playlist;
    }

    playlist.supported = true;
    playlist.media_sequence = !stream.segments.empty()
                                  ? stream.segments.front().sequence
                                  : stream.current_segment.sequence;
    int64_t max_duration_us = static_cast<int64_t>(hls_segment_duration_ms) * 1000;
    for (const StreamSegment &segment : stream.segments) {
        playlist.entries.push_back(
            StreamHlsEntry{segment.sequence, segment.duration_us});
        max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    if (stream.current_segment.published) {
        const int64_t duration_us = CurrentSegmentDurationUs(stream);
        playlist.entries.push_back(
            StreamHlsEntry{stream.current_segment.sequence, duration_us});
        max_duration_us = std::max(max_duration_us, duration_us);
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
    if (stream.current_segment.published &&
        stream.current_segment.sequence == sequence) {
        return BuildSegmentFromCurrent(stream);
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
    start_data.cached_gop_complete = stream.flv_gop_cache.complete;
    start_data.file_header = stream_mux::BuildFlvFileHeader();
    start_data.sequence_header = stream.sequence_header_tag;
    if (!stream.flv_gop_cache.complete) {
        start_data.config_generation = stream.config_generation;
        return start_data;
    }
    start_data.cached_video_tags.reserve(stream.flv_gop_cache.size);
    for (size_t i = 0; i < stream.flv_gop_cache.size; ++i) {
        const size_t index =
            (stream.flv_gop_cache.head + i) %
            stream.flv_gop_cache.frames.size();
        if (stream.flv_gop_cache.frames[index].slice_count != 0) {
            start_data.cached_video_tags.push_back(
                stream.flv_gop_cache.frames[index]);
        }
    }
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

    UpdateFrameTiming(stream, frame);

    bool keyframe = stream_codec::IsKeyFrame(frame.frame_type);
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
        ClearFlvGopCache(stream);
    }

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
        if (frame.codec == VideoCodec::kH265) {
            stream_mux::AppendH265NalUnitsToTsSegment(
                payload.h265_units, stream->vps, stream->sps, stream->pps,
                prepend_parameter_sets, frame.pts_us, frame.dts_us,
                &stream->ts_muxer_state, &stream->current_segment.body);
        } else {
            stream_mux::AppendH264NalUnitsToTsSegment(
                payload.h264_units, stream->sps, stream->pps,
                prepend_parameter_sets, frame.pts_us, frame.dts_us,
                &stream->ts_muxer_state, &stream->current_segment.body);
        }
        stream->current_segment.last_pts_us = frame.pts_us;
        if (keyframe) {
            result.hls_segment_updated =
                PublishCurrentSegment(stream, hls_playlist_depth);
        }
    }

    if (package_flv && frame.codec == VideoCodec::kH264) {
        const int64_t composition_time_ms = (frame.pts_us - frame.dts_us) / 1000;
        result.has_flv_tag_view = stream_mux::BuildH264FlvVideoTagView(
            keyframe, static_cast<int32_t>(composition_time_ms),
            static_cast<uint32_t>(frame.dts_us / 1000), payload.h264_units,
            &result.flv_tag_view);
        if (result.has_flv_tag_view) {
            PushFlvGopCache(stream, frame, keyframe, result.flv_tag_view);
        }
    } else if (package_flv && frame.codec == VideoCodec::kH265) {
        const int64_t composition_time_ms = (frame.pts_us - frame.dts_us) / 1000;
        result.has_flv_tag_view = stream_mux::BuildH265FlvVideoTagView(
            keyframe, static_cast<int32_t>(composition_time_ms),
            static_cast<uint32_t>(frame.dts_us / 1000), payload.h265_units,
            &result.flv_tag_view);
        if (result.has_flv_tag_view) {
            PushFlvGopCache(stream, frame, keyframe, result.flv_tag_view);
        }
    }

    result.keyframe = keyframe;
    result.accepted = true;
    return result;
}

}  // namespace stream_hub_internal
}  // namespace live_stream
