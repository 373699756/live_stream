#include "stream_frame_dispatcher.h"

#include "stream_codec.h"

namespace live_stream {
namespace stream_hub_internal {

StreamFrameSinkId StreamFrameDispatcher::Attach(
    const StreamFrameSinkOptions &options, IFrameSink *sink, size_t max_sinks) {
    if (sink == nullptr || frame_sinks_.size() >= max_sinks) {
        return 0;
    }
    const StreamFrameSinkId sink_id = next_frame_sink_id_++;
    FrameSinkState frame_sink;
    frame_sink.stream_id = options.stream_id;
    frame_sink.require_key_frame_first = options.require_key_frame_first;
    frame_sink.sink = sink;
    frame_sink.sink_name = options.sink_name;
    frame_sinks_[sink_id] = frame_sink;
    return sink_id;
}

bool StreamFrameDispatcher::Detach(StreamFrameSinkId sink_id) {
    return frame_sinks_.erase(sink_id) != 0;
}

void StreamFrameDispatcher::Clear() { frame_sinks_.clear(); }

size_t StreamFrameDispatcher::Size() const { return frame_sinks_.size(); }

std::vector<PendingStreamFrameSinkWrite>
StreamFrameDispatcher::CollectWrites(const EncodedFrame &frame) {
    std::vector<PendingStreamFrameSinkWrite> writes;
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
        PendingStreamFrameSinkWrite write;
        write.sink_id = item.first;
        write.sink = item.second.sink;
        writes.push_back(write);
    }
    return writes;
}

}  // namespace stream_hub_internal
}  // namespace live_stream
