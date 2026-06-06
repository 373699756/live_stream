#include "flv_client_registry.h"

#include <utility>

namespace live_stream {
namespace media_source_service_internal {

MediaFlvClientId FlvClientRegistry::Attach(
    StreamId stream_id, uint64_t config_generation, bool wait_for_keyframe,
    IMediaFlvSink *sink, size_t max_clients) {
    if (sink == nullptr || flv_clients_.size() >= max_clients) {
        return 0;
    }
    const MediaFlvClientId client_id = next_flv_client_id_++;
    FlvClientState client;
    client.stream_id = stream_id;
    client.config_generation = config_generation;
    client.wait_for_keyframe = wait_for_keyframe;
    client.sink = sink;
    flv_clients_[client_id] = client;
    return client_id;
}

bool FlvClientRegistry::Detach(MediaFlvClientId client_id) {
    auto iter = flv_clients_.find(client_id);
    if (iter == flv_clients_.end()) {
        return false;
    }
    iter->second.detached = true;
    (void)EraseDetachedClient(client_id, &iter->second);
    return true;
}

void FlvClientRegistry::Clear() {
    for (auto iter = flv_clients_.begin(); iter != flv_clients_.end();) {
        iter->second.detached = true;
        if (iter->second.pending_writes == 0) {
            ReleaseClientSink(&iter->second);
            iter = flv_clients_.erase(iter);
        } else {
            ++iter;
        }
    }
}

size_t FlvClientRegistry::Size() const { return flv_clients_.size(); }

bool FlvClientRegistry::HasClient(StreamId stream_id) const {
    for (const auto &item : flv_clients_) {
        if (!item.second.detached && item.second.stream_id == stream_id &&
            item.second.sink != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<PendingFlvClientWrite> FlvClientRegistry::CollectWrites(
    StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
    bool has_sequence_header, bool keyframe) {
    std::vector<PendingFlvClientWrite> writes;
    if (!has_flv_tag || !has_sequence_header) {
        return writes;
    }
    for (auto &item : flv_clients_) {
        if (item.second.detached || item.second.stream_id != stream_id ||
            item.second.sink == nullptr) {
            continue;
        }
        const bool starts_on_keyframe =
            item.second.wait_for_keyframe && keyframe;
        if (item.second.wait_for_keyframe && !keyframe) {
            continue;
        }
        if (keyframe) {
            item.second.wait_for_keyframe = false;
        }
        const bool needs_config =
            (starts_on_keyframe ||
             item.second.config_generation != config_generation) &&
            has_sequence_header;
        if (needs_config) {
            item.second.config_generation = config_generation;
        }
        PendingFlvClientWrite write;
        write.client_id = item.first;
        write.sink = item.second.sink;
        write.send_sequence_header = needs_config;
        write.starts_on_keyframe = starts_on_keyframe;
        writes.push_back(std::move(write));
        ++item.second.pending_writes;
    }
    return writes;
}

void FlvClientRegistry::ReleaseWrite(MediaFlvClientId client_id) {
    auto iter = flv_clients_.find(client_id);
    if (iter == flv_clients_.end()) {
        return;
    }
    if (iter->second.pending_writes > 0) {
        --iter->second.pending_writes;
    }
    (void)EraseDetachedClient(client_id, &iter->second);
}

void FlvClientRegistry::ReleaseClientSink(FlvClientState *client) {
    if (client == nullptr || client->sink == nullptr) {
        return;
    }
    delete client->sink;
    client->sink = nullptr;
}

bool FlvClientRegistry::EraseDetachedClient(MediaFlvClientId client_id,
                                            FlvClientState *client) {
    if (client == nullptr || !client->detached || client->pending_writes != 0) {
        return false;
    }
    ReleaseClientSink(client);
    return flv_clients_.erase(client_id) != 0;
}

}  // namespace media_source_service_internal
}  // namespace live_stream
