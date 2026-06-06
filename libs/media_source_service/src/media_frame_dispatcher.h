#ifndef LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_MEDIA_FRAME_DISPATCHER_H_
#define LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_MEDIA_FRAME_DISPATCHER_H_

#include "media/encoded_frame.h"
#include "media_source_service.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace live_stream {
namespace media_source_service_internal {

struct PendingMediaFrameSinkWrite {
    FrameAttachId sink_id = 0;
    IFrameSink *sink = nullptr;
};

// Maintains downstream frame sinks for one MediaSourceService instance. The
// owning service provides synchronization and calls this class under its mutex.
class MediaFrameDispatcher {
public:
    FrameAttachId Attach(const FrameAttachOptions &options,
                               IFrameSink *sink, size_t max_sinks);
    bool Detach(FrameAttachId sink_id);
    void Clear();
    size_t Size() const;
    std::vector<PendingMediaFrameSinkWrite> CollectWrites(
        const EncodedFrame &frame);

private:
    struct FrameSinkState {
        StreamId stream_id = StreamId::kMain;
        bool require_key_frame_first = true;
        IFrameSink *sink = nullptr;
        std::string sink_name;
    };

    std::map<FrameAttachId, FrameSinkState> frame_sinks_;
    FrameAttachId next_frame_sink_id_ = 1;
};

}  // namespace media_source_service_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_MEDIA_FRAME_DISPATCHER_H_
