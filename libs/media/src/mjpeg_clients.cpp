#include "mjpeg_clients.h"

#include <utility>

namespace live_stream {
namespace media_internal {

MediaMjpegClientId MjpegClients::Attach(
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

bool MjpegClients::Detach(MediaMjpegClientId client_id) {
    auto iter = mjpeg_clients_.find(client_id);
    if (iter == mjpeg_clients_.end()) {
        return false;
    }
    iter->second.detached = true;
    (void)EraseDetachedClient(client_id, &iter->second);
    return true;
}

void MjpegClients::Clear() {
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

size_t MjpegClients::Size() const { return mjpeg_clients_.size(); }

bool MjpegClients::IsStreamClientAttached(StreamId stream_id) const {
    for (const auto &item : mjpeg_clients_) {
        if (!item.second.detached && item.second.stream_id == stream_id &&
            item.second.sink != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<PendingMjpegClientWrite> MjpegClients::CollectWrites(
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

void MjpegClients::ReleaseWrite(MediaMjpegClientId client_id) {
    auto iter = mjpeg_clients_.find(client_id);
    if (iter == mjpeg_clients_.end()) {
        return;
    }
    if (iter->second.pending_writes > 0) {
        --iter->second.pending_writes;
    }
    (void)EraseDetachedClient(client_id, &iter->second);
}

void MjpegClients::ReleaseClientSink(MjpegClientState *client) {
    if (client == nullptr || client->sink == nullptr) {
        return;
    }
    delete client->sink;
    client->sink = nullptr;
}

bool MjpegClients::EraseDetachedClient(MediaMjpegClientId client_id,
                                       MjpegClientState *client) {
    if (client == nullptr || !client->detached || client->pending_writes != 0) {
        return false;
    }
    ReleaseClientSink(client);
    return mjpeg_clients_.erase(client_id) != 0;
}

}  // namespace media_internal
}  // namespace live_stream
