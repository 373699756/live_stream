#ifndef LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_MJPEG_CLIENT_REGISTRY_H_
#define LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_MJPEG_CLIENT_REGISTRY_H_

#include "media_pipeline.h"

#include <cstddef>
#include <map>
#include <vector>

namespace live_stream {
namespace media_source_service_internal {

struct PendingMjpegClientWrite {
    MediaMjpegClientId client_id = 0;
    IMediaMjpegSink *sink = nullptr;
};

// Tracks MJPEG browser clients with explicit sink ownership. The service owns
// the lock; writes increment pending_writes so detach cannot delete the sink
// during a callback.
class MjpegClientRegistry {
public:
    MediaMjpegClientId Attach(StreamId stream_id, IMediaMjpegSink *sink,
                               size_t max_clients);
    bool Detach(MediaMjpegClientId client_id);
    void Clear();
    size_t Size() const;
    bool HasClient(StreamId stream_id) const;
    std::vector<PendingMjpegClientWrite> CollectWrites(StreamId stream_id);
    void ReleaseWrite(MediaMjpegClientId client_id);

private:
    struct MjpegClientState {
        StreamId stream_id = StreamId::kMain;
        IMediaMjpegSink *sink = nullptr;
        uint32_t pending_writes = 0;
        bool detached = false;
    };

    static void ReleaseClientSink(MjpegClientState *client);
    bool EraseDetachedClient(MediaMjpegClientId client_id,
                             MjpegClientState *client);

    std::map<MediaMjpegClientId, MjpegClientState> mjpeg_clients_;
    MediaMjpegClientId next_mjpeg_client_id_ = 0x8000000000000001ULL;
};

}  // namespace media_source_service_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_MJPEG_CLIENT_REGISTRY_H_
