#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_SRC_STREAM_FRAME_DISPATCHER_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_SRC_STREAM_FRAME_DISPATCHER_H_

#include "media/encoded_frame.h"
#include "stream_hub_service.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace live_stream {
namespace stream_hub_internal {

struct PendingStreamFrameSinkWrite {
    StreamFrameSinkId sink_id = 0;
    IFrameSink *sink = nullptr;
};

// Maintains downstream frame sinks for one StreamHubService instance. The
// owning service provides synchronization and calls this class under its mutex.
class StreamFrameDispatcher {
public:
    StreamFrameSinkId Attach(const StreamFrameSinkOptions &options,
                             IFrameSink *sink, size_t max_sinks);
    bool Detach(StreamFrameSinkId sink_id);
    void Clear();
    size_t Size() const;
    std::vector<PendingStreamFrameSinkWrite> CollectWrites(
        const EncodedFrame &frame);

private:
    struct FrameSinkState {
        StreamId stream_id = StreamId::kMain;
        bool require_key_frame_first = true;
        IFrameSink *sink = nullptr;
        std::string sink_name;
    };

    std::map<StreamFrameSinkId, FrameSinkState> frame_sinks_;
    StreamFrameSinkId next_frame_sink_id_ = 1;
};

}  // namespace stream_hub_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_SRC_STREAM_FRAME_DISPATCHER_H_
