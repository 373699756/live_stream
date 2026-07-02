#ifndef LIVE_STREAM_MEDIA_SRC_FLV_CLIENTS_H_
#define LIVE_STREAM_MEDIA_SRC_FLV_CLIENTS_H_

#include "media/media_streams.h"

#include <cstddef>
#include <map>
#include <vector>

namespace live_stream {
namespace media_internal {

struct PendingFlvClientWrite {
    MediaFlvClientId client_id = 0;
    IMediaFlvSink *sink = nullptr;
    bool send_sequence_header = false;
    bool starts_on_keyframe = false;
};

// Tracks HTTP-FLV preview clients. PreviewClients owns the lock and calls all
// methods on this class under that lock.
class FlvClients {
public:
    MediaFlvClientId AttachClient(StreamId stream_id,
                                  uint64_t config_generation,
                                  bool wait_for_keyframe,
                                  IMediaFlvSink *sink,
                                  size_t max_clients);
    bool DetachClient(MediaFlvClientId client_id);
    uint32_t DetachStreamClients(StreamId stream_id);
    void Clear();
    size_t Size() const;
    bool IsStreamClientAttached(StreamId stream_id) const;
    std::vector<PendingFlvClientWrite> CollectWrites(
        StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
        bool has_sequence_header, bool keyframe);
    void ReleaseWrite(MediaFlvClientId client_id);

private:
    struct ClientState {
        StreamId stream_id = StreamId::kMain;
        uint64_t config_generation = 0;
        bool wait_for_keyframe = false;
        IMediaFlvSink *sink = nullptr;
        uint32_t pending_writes = 0;
        bool detached = false;
    };

    static void ReleaseClientSink(ClientState *client);
    bool EraseDetachedClient(MediaFlvClientId client_id,
                             ClientState *client);

    std::map<MediaFlvClientId, ClientState> clients_;
    MediaFlvClientId next_client_id_ = 1;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FLV_CLIENTS_H_
