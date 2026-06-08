#ifndef LIVE_STREAM_MEDIA_PIPELINE_SRC_MEDIA_PIPELINE_RUNTIME_DIAGNOSTICS_H_
#define LIVE_STREAM_MEDIA_PIPELINE_SRC_MEDIA_PIPELINE_RUNTIME_DIAGNOSTICS_H_

#include "media/stream_types.h"
#include "media_source.h"

#include <cstdint>

namespace live_stream {
namespace media_pipeline_internal {

struct MediaPipelineReadyState {
    bool hls_ready = false;
    bool flv_ready = false;
    bool mjpeg_ready = false;
};

struct MediaPipelineReadyChange {
    bool changed = false;
    bool ready = false;
    int32_t value = 0;
};

struct MediaPipelineFrameChange {
    bool first_frame_seen = false;
};

class MediaPipelineRuntimeDiagnostics {
public:
    void ResetStream(StreamId stream_id);
    void MarkKeyFrameRequest(StreamId stream_id);
    MediaPipelineFrameChange MarkFrame(StreamId stream_id, bool keyframe);
    MediaPipelineReadyChange MarkReady(StreamId stream_id,
                                       MediaPipelineReadyState ready_state);
    void FillStatus(StreamId stream_id, MediaSourceStatus *status) const;

private:
    struct StreamDiagnostics {
        bool hls_ready = false;
        bool flv_ready = false;
        bool mjpeg_ready = false;
        bool first_frame_seen = false;
        int64_t last_keyframe_request_ms = 0;
        int64_t last_keyframe_seen_ms = 0;
        int64_t last_first_frame_ms = 0;
        int64_t last_protocol_ready_ms = 0;
    };

    StreamDiagnostics *MutableStream(StreamId stream_id);
    const StreamDiagnostics *Stream(StreamId stream_id) const;

    StreamDiagnostics main_;
    StreamDiagnostics sub_;
};

}  // namespace media_pipeline_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_PIPELINE_SRC_MEDIA_PIPELINE_RUNTIME_DIAGNOSTICS_H_
