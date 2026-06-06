#include "media_source_stream_state.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace live_stream {
namespace media_source_internal {
bool IsBrowserCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265 ||
           codec == VideoCodec::kMjpeg;
}

namespace {

constexpr size_t kInitialHlsSegmentBytes = 256 * 1024;
constexpr size_t kMaxHlsSegmentBytes = 4 * 1024 * 1024;

uint32_t ClampHlsSegmentCapacity(size_t capacity) {
    if (capacity < kInitialHlsSegmentBytes) {
        return static_cast<uint32_t>(kInitialHlsSegmentBytes);
    }
    if (capacity > kMaxHlsSegmentBytes) {
        return static_cast<uint32_t>(kMaxHlsSegmentBytes);
    }
    return static_cast<uint32_t>(capacity);
}

void ClearFlvGopCache(StreamContext *stream) {
    if (stream == nullptr) {
        return;
    }
    for (MediaFlvCachedVideoTag &cached_tag : stream->flv_gop_cache.frames) {
        MediaFlvCachedVideoTagUnref(&cached_tag);
    }
    stream->flv_gop_cache = CachedFlvFrameRing{};
}

void UnrefHlsSegments(StreamContext *stream) {
    if (stream == nullptr) {
        return;
    }
    for (MediaSegmentRef &segment : stream->segments) {
        MediaSegmentRefUnref(&segment);
    }
    stream->segments.clear();
}

void HlsSegmentStateUnref(HlsSegmentState *segment) {
    if (segment == nullptr) {
        return;
    }
    VideoBufferUnref(segment->body);
    *segment = HlsSegmentState{};
}

bool EnsureHlsSegmentCapacity(HlsSegmentState *segment, size_t extra_bytes) {
    if (segment == nullptr || segment->body == nullptr ||
        segment->body->size > segment->body->capacity) {
        return false;
    }
    if (extra_bytes <= segment->body->capacity - segment->body->size) {
        return true;
    }
    uint32_t new_capacity = segment->body->capacity;
    while (extra_bytes > new_capacity - segment->body->size) {
        if (new_capacity >= kMaxHlsSegmentBytes) {
            return false;
        }
        const uint32_t doubled = new_capacity * 2U;
        new_capacity = doubled > new_capacity ? doubled : kMaxHlsSegmentBytes;
        if (new_capacity > kMaxHlsSegmentBytes) {
            new_capacity = kMaxHlsSegmentBytes;
        }
    }
    VideoBuffer *new_body = VideoBufferAlloc(new_capacity);
    if (new_body == nullptr) {
        return false;
    }
    std::copy(segment->body->data, segment->body->data + segment->body->size,
              new_body->data);
    if (!VideoBufferSetSize(new_body, segment->body->size)) {
        VideoBufferUnref(new_body);
        return false;
    }
    VideoBufferUnref(segment->body);
    segment->body = new_body;
    return true;
}

stream_mux::TsSegmentBuffer HlsSegmentBuffer(HlsSegmentState *segment) {
    stream_mux::TsSegmentBuffer buffer;
    if (segment == nullptr || segment->body == nullptr) {
        return buffer;
    }
    buffer.data = segment->body->data;
    buffer.capacity = segment->body->capacity;
    buffer.size = segment->body->size;
    return buffer;
}

bool CommitHlsSegmentBuffer(HlsSegmentState *segment,
                            const stream_mux::TsSegmentBuffer &buffer) {
    return segment != nullptr && segment->body != nullptr &&
           buffer.size <= segment->body->capacity &&
           VideoBufferSetSize(segment->body, static_cast<uint32_t>(buffer.size));
}

bool AppendFrameToHlsSegment(StreamContext *stream,
                             const ParsedFramePayload &payload,
                             bool prepend_parameter_sets,
                             const EncodedFrame &frame) {
    if (stream == nullptr || stream->current_segment.body == nullptr) {
        return false;
    }
    for (size_t attempt = 0; attempt < 8; ++attempt) {
        stream_mux::TsSegmentBuffer segment_body =
            HlsSegmentBuffer(&stream->current_segment);
        const size_t original_size = segment_body.size;
        stream_mux::TsMuxerState original_state = stream->ts_muxer_state;
        bool appended = false;
        if (frame.codec == VideoCodec::kH265) {
            appended = stream_mux::AppendH265NalUnitsToTsSegmentBuffer(
                payload.h265_units, stream->vps, stream->sps, stream->pps,
                prepend_parameter_sets, frame.pts_us, frame.dts_us,
                &stream->ts_muxer_state, &segment_body);
        } else {
            appended = stream_mux::AppendH264NalUnitsToTsSegmentBuffer(
                payload.h264_units, stream->sps, stream->pps,
                prepend_parameter_sets, frame.pts_us, frame.dts_us,
                &stream->ts_muxer_state, &segment_body);
        }
        if (appended && CommitHlsSegmentBuffer(&stream->current_segment,
                                               segment_body)) {
            return true;
        }
        stream->ts_muxer_state = original_state;
        (void)VideoBufferSetSize(stream->current_segment.body,
                                 static_cast<uint32_t>(original_size));
        if (!EnsureHlsSegmentCapacity(&stream->current_segment,
                                      stream->current_segment.body->capacity)) {
            return false;
        }
    }
    return false;
}

bool CopyFlvTagViewForCache(const EncodedFrame &frame,
                            const stream_mux::FlvVideoTagView &source,
                            MediaFlvCachedVideoTag *target) {
    if (target == nullptr || !EncodedFrameHasPayload(&frame) ||
        source.slice_count == 0 ||
        source.slice_count > kMaxMediaFlvVideoTagSlices) {
        return false;
    }

    MediaFlvCachedVideoTag cached_tag;
    if (!EncodedFrameRefCopy(&cached_tag.frame, &frame)) {
        return false;
    }
    cached_tag.slice_count = source.slice_count;
    cached_tag.total_size = source.total_size;
    cached_tag.timestamp_ms = source.timestamp_ms;
    for (size_t i = 0; i < source.slice_count; ++i) {
        const stream_mux::FlvVideoTagSlice &source_slice = source.slices[i];
        MediaFlvCachedVideoTagSlice &target_slice = cached_tag.slices[i];
        if (source_slice.data == nullptr || source_slice.size == 0) {
            MediaFlvCachedVideoTagUnref(&cached_tag);
            return false;
        }
        if (source_slice.media_payload) {
            const uint8_t *payload = EncodedFramePayloadData(&frame);
            const uintptr_t payload_addr =
                reinterpret_cast<uintptr_t>(payload);
            const uintptr_t source_addr =
                reinterpret_cast<uintptr_t>(source_slice.data);
            if (payload == nullptr || source_addr < payload_addr ||
                source_addr - payload_addr > frame.size ||
                source_slice.size > frame.size - (source_addr - payload_addr)) {
                MediaFlvCachedVideoTagUnref(&cached_tag);
                return false;
            }
            target_slice.media_data = source_slice.data;
            target_slice.size = source_slice.size;
            target_slice.media_payload = true;
        } else {
            if (source_slice.size > sizeof(target_slice.header_data)) {
                MediaFlvCachedVideoTagUnref(&cached_tag);
                return false;
            }
            std::copy(source_slice.data, source_slice.data + source_slice.size,
                      target_slice.header_data);
            target_slice.size = source_slice.size;
            target_slice.media_payload = false;
        }
    }

    MediaFlvCachedVideoTagUnref(target);
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
    HlsSegmentStateUnref(&stream->current_segment);
    stream->current_segment = HlsSegmentState{};
    stream->current_segment.started = true;
    stream->current_segment.sequence = stream->next_segment_sequence++;
    stream->current_segment.start_pts_us = pts_us;
    stream->current_segment.last_pts_us = pts_us;
    const uint32_t segment_capacity = ClampHlsSegmentCapacity(
        stream->next_hls_segment_capacity);
    stream->current_segment.body = VideoBufferAlloc(segment_capacity);
    if (stream->current_segment.body == nullptr) {
        HlsSegmentStateUnref(&stream->current_segment);
        return;
    }
    stream_mux::TsSegmentBuffer segment_body =
        HlsSegmentBuffer(&stream->current_segment);
    if (!stream_mux::AppendTsSegmentHeader(stream->codec,
                                           &stream->ts_muxer_state,
                                           &segment_body) ||
        !CommitHlsSegmentBuffer(&stream->current_segment, segment_body)) {
        HlsSegmentStateUnref(&stream->current_segment);
    }
}

void RememberHlsSegmentCapacity(StreamContext *stream,
                                const HlsSegmentState &segment) {
    if (stream == nullptr || segment.body == nullptr) {
        return;
    }
    stream->next_hls_segment_capacity =
        ClampHlsSegmentCapacity(segment.body->size);
}

void PopOldestSegment(StreamContext *stream) {
    if (stream == nullptr || stream->segments.empty()) {
        return;
    }
    MediaSegmentRefUnref(&stream->segments.front());
    stream->segments.pop_front();
}

void PushFinalizedSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body == nullptr ||
        stream->current_segment.body->size == 0) {
        return;
    }
    MediaSegmentRef segment;
    segment.found = true;
    segment.sequence = stream->current_segment.sequence;
    segment.duration_us = CurrentSegmentDurationUs(*stream);
    segment.body = stream->current_segment.body;
    RememberHlsSegmentCapacity(stream, stream->current_segment);
    stream->current_segment.body = nullptr;
    stream->segments.push_back(segment);
    while (stream->segments.size() > playlist_depth) {
        PopOldestSegment(stream);
    }
}

bool FinalizeCurrentSegment(StreamContext *stream, uint32_t playlist_depth) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body == nullptr ||
        stream->current_segment.body->size == 0) {
        return false;
    }

    PushFinalizedSegment(stream, playlist_depth);
    HlsSegmentStateUnref(&stream->current_segment);
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
           !stream.segments.empty();
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

void ParsedFramePayloadUnref(ParsedFramePayload *payload) {
    FramePayloadUnref(payload);
}

void ClearStreamContext(StreamContext *stream) {
    if (stream == nullptr) {
        return;
    }
    ClearFlvGopCache(stream);
    UnrefHlsSegments(stream);
    HlsSegmentStateUnref(&stream->current_segment);
    *stream = StreamContext{};
}

MediaHlsPlaylist BuildHlsPlaylist(const StreamContext &stream,
                                   uint32_t hls_segment_duration_ms,
                                   uint32_t hls_playlist_depth) {
    MediaHlsPlaylist playlist;
    if (!IsHlsStreamReady(stream)) {
        return playlist;
    }

    playlist.supported = true;
    const size_t playlist_depth =
        std::max<size_t>(1, static_cast<size_t>(hls_playlist_depth));
    const size_t start_index =
        stream.segments.size() > playlist_depth
            ? stream.segments.size() - playlist_depth
            : 0;
    playlist.media_sequence = stream.segments[start_index].sequence;
    int64_t max_duration_us = static_cast<int64_t>(hls_segment_duration_ms) * 1000;
    for (size_t i = start_index; i < stream.segments.size(); ++i) {
        const MediaSegmentRef &segment = stream.segments[i];
        playlist.entries.push_back(
            MediaHlsEntry{segment.sequence, segment.duration_us});
        max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    playlist.target_duration_sec = static_cast<uint32_t>(
        std::max<int64_t>(1, (max_duration_us + 999999) / 1000000));
    return playlist;
}

MediaSegmentRef FindHlsSegmentRef(const StreamContext &stream,
                                   uint64_t sequence) {
    if (!IsBrowserStreamReady(stream.state, stream.codec)) {
        return MediaSegmentRef{};
    }

    for (const MediaSegmentRef &segment : stream.segments) {
        if (segment.sequence == sequence) {
            return MediaSegmentRefCopy(&segment);
        }
    }
    return MediaSegmentRef{};
}

MediaFlvStartData BuildFlvStartData(const StreamContext &stream) {
    MediaFlvStartData start_data;
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
            MediaFlvCachedVideoTag cached_tag;
            if (MediaFlvCachedVideoTagRefCopy(
                    &cached_tag, &stream.flv_gop_cache.frames[index])) {
                start_data.cached_video_tags.push_back(cached_tag);
            }
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
        if (!AppendFrameToHlsSegment(stream, payload, prepend_parameter_sets,
                                     frame)) {
            HlsSegmentStateUnref(&stream->current_segment);
        } else {
            stream->current_segment.last_pts_us = frame.pts_us;
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

}  // namespace media_source_internal
}  // namespace live_stream
