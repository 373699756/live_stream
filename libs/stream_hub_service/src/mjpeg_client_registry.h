#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_SRC_MJPEG_CLIENT_REGISTRY_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_SRC_MJPEG_CLIENT_REGISTRY_H_

#include "stream_hub_service.h"

#include <cstddef>
#include <map>
#include <vector>

namespace live_stream {
namespace stream_hub_internal {

struct PendingMjpegClientWrite {
    StreamMjpegClientId client_id = 0;
    IStreamMjpegSink *sink = nullptr;
};

// Tracks MJPEG browser clients with explicit sink ownership. The service owns
// the lock; writes increment pending_writes so detach cannot delete the sink
// during a callback.
class MjpegClientRegistry {
public:
    StreamMjpegClientId Attach(StreamId stream_id, IStreamMjpegSink *sink,
                               size_t max_clients);
    bool Detach(StreamMjpegClientId client_id);
    void Clear();
    size_t Size() const;
    bool HasClient(StreamId stream_id) const;
    std::vector<PendingMjpegClientWrite> CollectWrites(StreamId stream_id);
    void ReleaseWrite(StreamMjpegClientId client_id);

private:
    struct MjpegClientState {
        StreamId stream_id = StreamId::kMain;
        IStreamMjpegSink *sink = nullptr;
        uint32_t pending_writes = 0;
        bool detached = false;
    };

    static void ReleaseClientSink(MjpegClientState *client);
    bool EraseDetachedClient(StreamMjpegClientId client_id,
                             MjpegClientState *client);

    std::map<StreamMjpegClientId, MjpegClientState> mjpeg_clients_;
    StreamMjpegClientId next_mjpeg_client_id_ = 0x8000000000000001ULL;
};

}  // namespace stream_hub_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_SRC_MJPEG_CLIENT_REGISTRY_H_
