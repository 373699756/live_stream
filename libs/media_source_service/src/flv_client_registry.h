#ifndef LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_FLV_CLIENT_REGISTRY_H_
#define LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_FLV_CLIENT_REGISTRY_H_

#include "media_source_service.h"

#include <cstddef>
#include <map>
#include <vector>

namespace live_stream {
namespace media_source_service_internal {

struct PendingFlvClientWrite {
    MediaFlvClientId client_id = 0;
    IMediaFlvSink *sink = nullptr;
    bool send_sequence_header = false;
    bool starts_on_keyframe = false;
};

// Tracks active HTTP-FLV clients. The owner synchronizes access and only keeps
// raw sink pointers here. Attach owns the sink on success; writes increment a
// pending count so Detach/Clear cannot delete a sink while it is being called.
class FlvClientRegistry {
public:
    MediaFlvClientId Attach(StreamId stream_id, uint64_t config_generation,
                             bool wait_for_keyframe, IMediaFlvSink *sink,
                             size_t max_clients);
    bool Detach(MediaFlvClientId client_id);
    void Clear();
    size_t Size() const;
    bool HasClient(StreamId stream_id) const;
    std::vector<PendingFlvClientWrite> CollectWrites(
        StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
        bool has_sequence_header, bool keyframe);
    void ReleaseWrite(MediaFlvClientId client_id);

private:
    struct FlvClientState {
        StreamId stream_id = StreamId::kMain;
        uint64_t config_generation = 0;
        bool wait_for_keyframe = false;
        IMediaFlvSink *sink = nullptr;
        uint32_t pending_writes = 0;
        bool detached = false;
    };

    static void ReleaseClientSink(FlvClientState *client);
    bool EraseDetachedClient(MediaFlvClientId client_id,
                             FlvClientState *client);

    std::map<MediaFlvClientId, FlvClientState> flv_clients_;
    MediaFlvClientId next_flv_client_id_ = 1;
};

}  // namespace media_source_service_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SERVICE_SRC_FLV_CLIENT_REGISTRY_H_
