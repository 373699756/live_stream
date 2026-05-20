#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_SRC_FLV_CLIENT_REGISTRY_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_SRC_FLV_CLIENT_REGISTRY_H_

#include "stream_hub_service.h"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace live_stream {
namespace stream_hub_internal {

struct PendingFlvClientWrite {
    StreamFlvClientId client_id = 0;
    std::shared_ptr<IStreamFlvSink> sink;
    bool send_sequence_header = false;
};

// Tracks active HTTP-FLV clients. The owner synchronizes access and only keeps
// sink shared_ptrs here; network writes are performed outside the service lock.
class FlvClientRegistry {
public:
    StreamFlvClientId Attach(StreamId stream_id, uint64_t config_generation,
                             const std::shared_ptr<IStreamFlvSink> &sink,
                             size_t max_clients);
    bool Detach(StreamFlvClientId client_id);
    void Clear();
    size_t Size() const;
    std::vector<PendingFlvClientWrite> CollectWrites(
        StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
        bool has_sequence_header, bool has_new_sequence_header);

private:
    struct FlvClientState {
        StreamId stream_id = StreamId::kMain;
        uint64_t config_generation = 0;
        std::shared_ptr<IStreamFlvSink> sink;
    };

    std::map<StreamFlvClientId, FlvClientState> flv_clients_;
    StreamFlvClientId next_flv_client_id_ = 1;
};

}  // namespace stream_hub_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_SRC_FLV_CLIENT_REGISTRY_H_
