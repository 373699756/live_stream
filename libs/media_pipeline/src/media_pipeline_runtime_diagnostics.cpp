#include "media_pipeline_runtime_diagnostics.h"

#include "infra/time.h"

namespace live_stream {
namespace media_pipeline_internal {
namespace {

int32_t ReadyStateValue(const MediaPipelineReadyState &ready_state) {
    int32_t value = 0;
    if (ready_state.hls_ready) {
        value |= 1;
    }
    if (ready_state.flv_ready) {
        value |= 2;
    }
    if (ready_state.mjpeg_ready) {
        value |= 4;
    }
    return value;
}

}  // namespace

void MediaPipelineRuntimeDiagnostics::ResetStream(StreamId stream_id) {
    StreamDiagnostics *diagnostics = MutableStream(stream_id);
    if (diagnostics == nullptr) {
        return;
    }
    diagnostics->hls_ready = false;
    diagnostics->flv_ready = false;
    diagnostics->mjpeg_ready = false;
    diagnostics->first_frame_seen = false;
    diagnostics->last_first_frame_ms = 0;
    diagnostics->last_protocol_ready_ms = 0;
}

void MediaPipelineRuntimeDiagnostics::MarkKeyFrameRequest(
    StreamId stream_id) {
    StreamDiagnostics *diagnostics = MutableStream(stream_id);
    if (diagnostics == nullptr) {
        return;
    }
    diagnostics->last_keyframe_request_ms = infra::Time::SystemTimeMillis();
}

MediaPipelineFrameChange MediaPipelineRuntimeDiagnostics::MarkFrame(
    StreamId stream_id, bool keyframe) {
    MediaPipelineFrameChange change;
    StreamDiagnostics *diagnostics = MutableStream(stream_id);
    if (diagnostics == nullptr) {
        return change;
    }
    const int64_t now_ms = infra::Time::SystemTimeMillis();
    if (!diagnostics->first_frame_seen) {
        diagnostics->first_frame_seen = true;
        diagnostics->last_first_frame_ms = now_ms;
        change.first_frame_seen = true;
    }
    if (keyframe) {
        diagnostics->last_keyframe_seen_ms = now_ms;
        change.keyframe_seen = true;
    }
    return change;
}

MediaPipelineReadyChange MediaPipelineRuntimeDiagnostics::MarkReady(
    StreamId stream_id, MediaPipelineReadyState ready_state) {
    MediaPipelineReadyChange change;
    StreamDiagnostics *diagnostics = MutableStream(stream_id);
    if (diagnostics == nullptr) {
        return change;
    }
    change.changed = diagnostics->hls_ready != ready_state.hls_ready ||
                     diagnostics->flv_ready != ready_state.flv_ready ||
                     diagnostics->mjpeg_ready != ready_state.mjpeg_ready;
    diagnostics->hls_ready = ready_state.hls_ready;
    diagnostics->flv_ready = ready_state.flv_ready;
    diagnostics->mjpeg_ready = ready_state.mjpeg_ready;
    change.ready = ready_state.hls_ready || ready_state.flv_ready ||
                   ready_state.mjpeg_ready;
    change.value = ReadyStateValue(ready_state);
    if (change.changed && change.ready) {
        diagnostics->last_protocol_ready_ms =
            infra::Time::SystemTimeMillis();
    }
    return change;
}

void MediaPipelineRuntimeDiagnostics::FillStatus(
    StreamId stream_id, MediaSourceStatus *status) const {
    if (status == nullptr) {
        return;
    }
    const StreamDiagnostics *diagnostics = Stream(stream_id);
    if (diagnostics == nullptr) {
        return;
    }
    status->last_keyframe_request_ms =
        diagnostics->last_keyframe_request_ms;
    status->last_keyframe_seen_ms = diagnostics->last_keyframe_seen_ms;
    status->last_first_frame_ms = diagnostics->last_first_frame_ms;
    status->last_protocol_ready_ms = diagnostics->last_protocol_ready_ms;
}

MediaPipelineRuntimeDiagnostics::StreamDiagnostics *
MediaPipelineRuntimeDiagnostics::MutableStream(StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
        return &main_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_;
    }
    return nullptr;
}

const MediaPipelineRuntimeDiagnostics::StreamDiagnostics *
MediaPipelineRuntimeDiagnostics::Stream(StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
        return &main_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_;
    }
    return nullptr;
}

}  // namespace media_pipeline_internal
}  // namespace live_stream
