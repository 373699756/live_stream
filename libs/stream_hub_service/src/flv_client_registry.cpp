#include "flv_client_registry.h"

#include <utility>

namespace live_stream {
namespace stream_hub_internal {

StreamFlvClientId FlvClientRegistry::Attach(
    StreamId stream_id, uint64_t config_generation,
    const std::shared_ptr<IStreamFlvSink> &sink, size_t max_clients) {
    if (sink == nullptr || flv_clients_.size() >= max_clients) {
        return 0;
    }
    const StreamFlvClientId client_id = next_flv_client_id_++;
    FlvClientState client;
    client.stream_id = stream_id;
    client.config_generation = config_generation;
    client.sink = sink;
    flv_clients_[client_id] = client;
    return client_id;
}

bool FlvClientRegistry::Detach(StreamFlvClientId client_id) {
    return flv_clients_.erase(client_id) != 0;
}

void FlvClientRegistry::Clear() { flv_clients_.clear(); }

size_t FlvClientRegistry::Size() const { return flv_clients_.size(); }

std::vector<PendingFlvClientWrite> FlvClientRegistry::CollectWrites(
    StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
    bool has_sequence_header, bool has_new_sequence_header) {
    std::vector<PendingFlvClientWrite> writes;
    if (!has_flv_tag || !has_sequence_header) {
        return writes;
    }
    for (auto &item : flv_clients_) {
        if (item.second.stream_id != stream_id || item.second.sink == nullptr) {
            continue;
        }
        const bool needs_config =
            item.second.config_generation != config_generation &&
            has_new_sequence_header;
        if (needs_config) {
            item.second.config_generation = config_generation;
        }
        PendingFlvClientWrite write;
        write.client_id = item.first;
        write.sink = item.second.sink;
        write.send_sequence_header = needs_config;
        writes.push_back(std::move(write));
    }
    return writes;
}

}  // namespace stream_hub_internal
}  // namespace live_stream
