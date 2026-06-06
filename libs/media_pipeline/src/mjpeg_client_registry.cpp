#include "mjpeg_client_registry.h"

#include <utility>

namespace live_stream {
namespace media_pipeline_internal {

MediaMjpegClientId MjpegClientRegistry::Attach(
    StreamId stream_id, IMediaMjpegSink *sink, size_t max_clients) {
    if (sink == nullptr || mjpeg_clients_.size() >= max_clients) {
        return 0;
    }
    const MediaMjpegClientId client_id = next_mjpeg_client_id_++;
    MjpegClientState client;
    client.stream_id = stream_id;
    client.sink = sink;
    mjpeg_clients_[client_id] = client;
    return client_id;
}

bool MjpegClientRegistry::Detach(MediaMjpegClientId client_id) {
    auto iter = mjpeg_clients_.find(client_id);
    if (iter == mjpeg_clients_.end()) {
        return false;
    }
    iter->second.detached = true;
    (void)EraseDetachedClient(client_id, &iter->second);
    return true;
}

void MjpegClientRegistry::Clear() {
    for (auto iter = mjpeg_clients_.begin(); iter != mjpeg_clients_.end();) {
        iter->second.detached = true;
        if (iter->second.pending_writes == 0) {
            ReleaseClientSink(&iter->second);
            iter = mjpeg_clients_.erase(iter);
        } else {
            ++iter;
        }
    }
}

size_t MjpegClientRegistry::Size() const { return mjpeg_clients_.size(); }

bool MjpegClientRegistry::HasClient(StreamId stream_id) const {
    for (const auto &item : mjpeg_clients_) {
        if (!item.second.detached && item.second.stream_id == stream_id &&
            item.second.sink != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<PendingMjpegClientWrite> MjpegClientRegistry::CollectWrites(
    StreamId stream_id) {
    std::vector<PendingMjpegClientWrite> writes;
    for (auto &item : mjpeg_clients_) {
        if (item.second.detached || item.second.stream_id != stream_id ||
            item.second.sink == nullptr) {
            continue;
        }
        PendingMjpegClientWrite write;
        write.client_id = item.first;
        write.sink = item.second.sink;
        writes.push_back(std::move(write));
        ++item.second.pending_writes;
    }
    return writes;
}

void MjpegClientRegistry::ReleaseWrite(MediaMjpegClientId client_id) {
    auto iter = mjpeg_clients_.find(client_id);
    if (iter == mjpeg_clients_.end()) {
        return;
    }
    if (iter->second.pending_writes > 0) {
        --iter->second.pending_writes;
    }
    (void)EraseDetachedClient(client_id, &iter->second);
}

void MjpegClientRegistry::ReleaseClientSink(MjpegClientState *client) {
    if (client == nullptr || client->sink == nullptr) {
        return;
    }
    delete client->sink;
    client->sink = nullptr;
}

bool MjpegClientRegistry::EraseDetachedClient(MediaMjpegClientId client_id,
                                              MjpegClientState *client) {
    if (client == nullptr || !client->detached || client->pending_writes != 0) {
        return false;
    }
    ReleaseClientSink(client);
    return mjpeg_clients_.erase(client_id) != 0;
}

}  // namespace media_pipeline_internal
}  // namespace live_stream
