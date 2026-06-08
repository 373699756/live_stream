#include "frame_sinks.h"

#include "media_channels.h"

namespace live_stream {
namespace device_media_internal {

FrameAttachId FrameAttachments::ReserveId() { return next_attach_id_++; }

void FrameAttachments::Add(FrameAttachId id,
                           const FrameAttachOptions &options,
                           IFrameSink *sink) {
    sinks_[id] = std::make_pair(options, sink);
}

bool FrameAttachments::Remove(FrameAttachId id) {
    auto it = sinks_.find(id);
    if (it == sinks_.end()) {
        return false;
    }
    sinks_.erase(it);
    return true;
}

std::vector<IFrameSink *> FrameAttachments::CollectSinks(
    StreamId stream_id) const {
    std::vector<IFrameSink *> sinks;
    for (const auto &item : sinks_) {
        if (item.second.first.stream_id == stream_id &&
            item.second.second != nullptr) {
            sinks.push_back(item.second.second);
        }
    }
    return sinks;
}

std::vector<FrameAttachments::AttachedSink>
FrameAttachments::CollectAttachedSinks() const {
    std::vector<AttachedSink> attached_sinks;
    for (const auto &item : sinks_) {
        if (item.second.second == nullptr) {
            continue;
        }
        AttachedSink attached_sink;
        attached_sink.sink = item.second.second;
        attached_sink.stream_id = item.second.first.stream_id;
        attached_sinks.push_back(attached_sink);
    }
    return attached_sinks;
}

std::vector<FrameAttachments::SourceStateNotice> BuildSourceStateEvents(
    const FrameAttachments &frame_attachments,
    const MediaPipelineConfig &active_config,
    StreamState stream_state) {
    std::vector<FrameAttachments::SourceStateNotice> events;
    const std::vector<FrameAttachments::AttachedSink> targets =
        frame_attachments.CollectAttachedSinks();
    for (const FrameAttachments::AttachedSink &target : targets) {
        const VideoStreamConfig *stream =
            FindConfiguredStream(active_config, target.stream_id);
        const bool stream_running =
            stream_state == StreamState::kRunning && stream != nullptr &&
            stream->enabled;
        FrameAttachments::SourceStateNotice event;
        event.sink = target.sink;
        event.stream_id = target.stream_id;
        event.state = stream_running ? stream_state : StreamState::kClosed;
        events.push_back(event);
    }
    return events;
}

void NotifySourceState(
    const std::vector<FrameAttachments::SourceStateNotice>
        &source_state_events) {
    for (const FrameAttachments::SourceStateNotice &notification :
         source_state_events) {
        if (notification.sink != nullptr) {
            notification.sink->OnSourceStateChanged(notification.stream_id,
                                                    notification.state);
        }
    }
}

}  // namespace device_media_internal
}  // namespace live_stream
