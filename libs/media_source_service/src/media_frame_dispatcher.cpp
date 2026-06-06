#include "media_frame_dispatcher.h"

#include "stream_codec.h"

namespace live_stream {
namespace media_source_service_internal {

FrameAttachId MediaFrameDispatcher::Attach(
    const FrameAttachOptions &options, IFrameSink *sink,
    size_t max_sinks) {
    if (sink == nullptr || frame_sinks_.size() >= max_sinks) {
        return 0;
    }
    const FrameAttachId sink_id = next_frame_sink_id_++;
    FrameSinkState frame_sink;
    frame_sink.stream_id = options.stream_id;
    frame_sink.require_key_frame_first = options.require_key_frame_first;
    frame_sink.sink = sink;
    frame_sink.sink_name = options.sink_name;
    frame_sinks_[sink_id] = frame_sink;
    return sink_id;
}

bool MediaFrameDispatcher::Detach(FrameAttachId sink_id) {
    return frame_sinks_.erase(sink_id) != 0;
}

void MediaFrameDispatcher::Clear() { frame_sinks_.clear(); }

size_t MediaFrameDispatcher::Size() const { return frame_sinks_.size(); }

std::vector<PendingMediaFrameSinkWrite>
MediaFrameDispatcher::CollectWrites(const EncodedFrame &frame) {
    std::vector<PendingMediaFrameSinkWrite> writes;
    for (auto &item : frame_sinks_) {
        if (item.second.stream_id != frame.stream_id ||
            item.second.sink == nullptr) {
            continue;
        }
        if (item.second.require_key_frame_first &&
            !stream_codec::IsKeyFrame(frame.frame_type)) {
            continue;
        }
        item.second.require_key_frame_first = false;
        PendingMediaFrameSinkWrite write;
        write.sink_id = item.first;
        write.sink = item.second.sink;
        writes.push_back(write);
    }
    return writes;
}

}  // namespace media_source_service_internal
}  // namespace live_stream
