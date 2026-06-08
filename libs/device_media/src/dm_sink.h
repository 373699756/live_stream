#ifndef LIVE_STREAM_DEVICE_MEDIA_SRC_DM_SINK_H_
#define LIVE_STREAM_DEVICE_MEDIA_SRC_DM_SINK_H_

#include "media/frame_attach.h"
#include "media/pipeline_config.h"

#include <map>
#include <utility>
#include <vector>

namespace live_stream {
namespace device_media_internal {

class FrameAttachments {
public:
    struct AttachedSink {
        IFrameSink *sink = nullptr;
        StreamId stream_id = StreamId::kMain;
    };

    struct SourceStateNotice {
        IFrameSink *sink = nullptr;
        StreamId stream_id = StreamId::kMain;
        StreamState state = StreamState::kClosed;
    };

    FrameAttachId ReserveId();
    void Add(FrameAttachId id, const FrameAttachOptions &options,
             IFrameSink *sink);
    bool Remove(FrameAttachId id);
    std::vector<IFrameSink *> CollectSinks(StreamId stream_id) const;
    std::vector<AttachedSink> CollectAttachedSinks() const;

private:
    std::map<FrameAttachId, std::pair<FrameAttachOptions, IFrameSink *>>
        sinks_;
    FrameAttachId next_attach_id_ = 1;
};

std::vector<FrameAttachments::SourceStateNotice> BuildSourceStateEvents(
    const FrameAttachments &frame_attachments,
    const MediaPipelineConfig &active_config,
    StreamState stream_state);
void NotifySourceState(
    const std::vector<FrameAttachments::SourceStateNotice>
        &source_state_events);

}  // namespace device_media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_SRC_DM_SINK_H_
