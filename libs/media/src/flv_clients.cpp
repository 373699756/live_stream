#include "flv_clients.h"

#include <utility>

namespace live_stream {
namespace media_internal {

MediaFlvClientId FlvClients::AttachClient(
    StreamId stream_id, uint64_t config_generation, bool wait_for_keyframe,
    IMediaFlvSink *sink, size_t max_clients) {
    if (sink == nullptr || clients_.size() >= max_clients) {
        return 0;
    }
    const MediaFlvClientId client_id = next_client_id_++;
    ClientState client;
    client.stream_id = stream_id;
    client.config_generation = config_generation;
    client.wait_for_keyframe = wait_for_keyframe;
    client.sink = sink;
    clients_[client_id] = client;
    return client_id;
}

bool FlvClients::DetachClient(MediaFlvClientId client_id) {
    auto iter = clients_.find(client_id);
    if (iter == clients_.end()) {
        return false;
    }
    iter->second.detached = true;
    (void)EraseDetachedClient(client_id, &iter->second);
    return true;
}

uint32_t FlvClients::DetachStreamClients(StreamId stream_id) {
    uint32_t detached = 0;
    for (auto iter = clients_.begin(); iter != clients_.end();) {
        ClientState &client = iter->second;
        if (!client.detached && client.stream_id == stream_id) {
            client.detached = true;
            ++detached;
        }
        if (client.detached && client.pending_writes == 0) {
            ReleaseClientSink(&client);
            iter = clients_.erase(iter);
        } else {
            ++iter;
        }
    }
    return detached;
}

void FlvClients::Clear() {
    for (auto iter = clients_.begin(); iter != clients_.end();) {
        iter->second.detached = true;
        if (iter->second.pending_writes == 0) {
            ReleaseClientSink(&iter->second);
            iter = clients_.erase(iter);
        } else {
            ++iter;
        }
    }
}

size_t FlvClients::Size() const { return clients_.size(); }

bool FlvClients::IsStreamClientAttached(StreamId stream_id) const {
    for (const auto &item : clients_) {
        if (!item.second.detached && item.second.stream_id == stream_id &&
            item.second.sink != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<PendingFlvClientWrite> FlvClients::CollectWrites(
    StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
    bool has_sequence_header, bool keyframe) {
    std::vector<PendingFlvClientWrite> writes;
    if (!has_flv_tag || !has_sequence_header) {
        return writes;
    }
    for (auto &item : clients_) {
        ClientState &client = item.second;
        if (client.detached || client.stream_id != stream_id ||
            client.sink == nullptr) {
            continue;
        }
        const bool starts_on_keyframe = client.wait_for_keyframe && keyframe;
        if (client.wait_for_keyframe && !keyframe) {
            continue;
        }
        if (keyframe) {
            client.wait_for_keyframe = false;
        }
        const bool needs_config =
            (starts_on_keyframe ||
             client.config_generation != config_generation) &&
            has_sequence_header;
        if (needs_config) {
            client.config_generation = config_generation;
        }
        PendingFlvClientWrite write;
        write.client_id = item.first;
        write.sink = client.sink;
        write.send_sequence_header = needs_config;
        write.starts_on_keyframe = starts_on_keyframe;
        writes.push_back(std::move(write));
        ++client.pending_writes;
    }
    return writes;
}

void FlvClients::ReleaseWrite(MediaFlvClientId client_id) {
    auto iter = clients_.find(client_id);
    if (iter == clients_.end()) {
        return;
    }
    if (iter->second.pending_writes > 0) {
        --iter->second.pending_writes;
    }
    (void)EraseDetachedClient(client_id, &iter->second);
}

void FlvClients::ReleaseClientSink(ClientState *client) {
    if (client == nullptr || client->sink == nullptr) {
        return;
    }
    client->sink->Close();
    delete client->sink;
    client->sink = nullptr;
}

bool FlvClients::EraseDetachedClient(MediaFlvClientId client_id,
                                      ClientState *client) {
    if (client == nullptr || !client->detached ||
        client->pending_writes != 0) {
        return false;
    }
    ReleaseClientSink(client);
    return clients_.erase(client_id) != 0;
}

}  // namespace media_internal
}  // namespace live_stream
