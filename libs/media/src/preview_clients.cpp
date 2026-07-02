#include "preview_clients.h"

#include <utility>
#include <vector>

namespace live_stream {
namespace media_internal {

class PreviewClients::FlvWriteLease {
public:
    FlvWriteLease(PreviewClients *clients, MediaFlvClientId client_id)
        : clients_(clients), client_id_(client_id) {}

    FlvWriteLease(const FlvWriteLease &) = delete;
    FlvWriteLease &operator=(const FlvWriteLease &) = delete;

    ~FlvWriteLease() {
        if (clients_ != nullptr && client_id_ != 0) {
            clients_->ReleaseFlvWrite(client_id_);
        }
    }

private:
    PreviewClients *clients_ = nullptr;
    MediaFlvClientId client_id_ = 0;
};

class PreviewClients::MjpegWriteLease {
public:
    MjpegWriteLease(PreviewClients *clients, MediaMjpegClientId client_id)
        : clients_(clients), client_id_(client_id) {}

    MjpegWriteLease(const MjpegWriteLease &) = delete;
    MjpegWriteLease &operator=(const MjpegWriteLease &) = delete;

    ~MjpegWriteLease() {
        if (clients_ != nullptr && client_id_ != 0) {
            clients_->ReleaseMjpegWrite(client_id_);
        }
    }

private:
    PreviewClients *clients_ = nullptr;
    MediaMjpegClientId client_id_ = 0;
};

void PreviewClients::Clear() {
    std::lock_guard<std::mutex> guard(mutex_);
    flv_clients_.Clear();
    mjpeg_clients_.Clear();
}

size_t PreviewClients::FlvSize() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return flv_clients_.Size();
}

size_t PreviewClients::MjpegSize() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return mjpeg_clients_.Size();
}

MediaFlvClientId PreviewClients::AttachFlv(
    StreamId stream_id,
    uint64_t config_generation,
    bool wait_for_keyframe,
    IMediaFlvSink *sink,
    size_t max_clients) {
    std::lock_guard<std::mutex> guard(mutex_);
    return flv_clients_.AttachClient(stream_id, config_generation,
                                     wait_for_keyframe, sink, max_clients);
}

bool PreviewClients::DetachFlv(MediaFlvClientId client_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    return flv_clients_.DetachClient(client_id);
}

uint32_t PreviewClients::DetachFlvStream(StreamId stream_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    return flv_clients_.DetachStreamClients(stream_id);
}

bool PreviewClients::HasFlvClient(StreamId stream_id) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return flv_clients_.IsStreamClientAttached(stream_id);
}

void PreviewClients::WriteFlv(StreamId stream_id,
                              uint64_t config_generation,
                              bool keyframe,
                              const std::string &sequence_header_tag,
                              const FlvVideoTagBuild &flv_tag_view,
                              bool has_flv_tag_view,
                              const MediaFrame &frame) {
    std::vector<PendingFlvClientWrite> clients;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const bool has_sequence_header = !sequence_header_tag.empty();
        clients = flv_clients_.CollectWrites(
            stream_id, config_generation, has_flv_tag_view,
            has_sequence_header, keyframe);
    }

    std::vector<MediaFlvClientId> detach_ids;
    for (const PendingFlvClientWrite &client : clients) {
        bool detach_client = false;
        {
            FlvWriteLease write_lease(this, client.client_id);
            if (client.sink == nullptr) {
                detach_client = true;
            } else if (client.send_sequence_header &&
                       !client.sink->OnFlvChunk(
                           reinterpret_cast<const uint8_t *>(
                               sequence_header_tag.data()),
                           sequence_header_tag.size())) {
                detach_client = true;
            } else if (!has_flv_tag_view) {
                detach_client = true;
            } else if (!client.sink->OnFlvVideoTag(flv_tag_view.view, frame)) {
                detach_client = true;
            }
        }
        if (detach_client) {
            detach_ids.push_back(client.client_id);
        }
    }

    for (MediaFlvClientId client_id : detach_ids) {
        if (client_id != 0) {
            (void)DetachFlv(client_id);
        }
    }
}

MediaMjpegClientId PreviewClients::AttachMjpeg(StreamId stream_id,
                                               IMediaMjpegSink *sink,
                                               size_t max_clients) {
    std::lock_guard<std::mutex> guard(mutex_);
    return mjpeg_clients_.Attach(stream_id, sink, max_clients);
}

bool PreviewClients::DetachMjpeg(MediaMjpegClientId client_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    return mjpeg_clients_.Detach(client_id);
}

bool PreviewClients::HasMjpegClient(StreamId stream_id) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return mjpeg_clients_.IsStreamClientAttached(stream_id);
}

void PreviewClients::WriteMjpeg(StreamId stream_id, const MediaFrame &frame) {
    std::vector<PendingMjpegClientWrite> clients;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        clients = mjpeg_clients_.CollectWrites(stream_id);
    }

    std::vector<MediaMjpegClientId> detach_ids;
    for (const PendingMjpegClientWrite &client : clients) {
        bool detach_client = false;
        {
            MjpegWriteLease write_lease(this, client.client_id);
            detach_client =
                client.sink == nullptr || !client.sink->OnMjpegFrame(frame);
        }
        if (detach_client) {
            detach_ids.push_back(client.client_id);
        }
    }

    for (MediaMjpegClientId client_id : detach_ids) {
        if (client_id != 0) {
            (void)DetachMjpeg(client_id);
        }
    }
}

void PreviewClients::ReleaseFlvWrite(MediaFlvClientId client_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    flv_clients_.ReleaseWrite(client_id);
}

void PreviewClients::ReleaseMjpegWrite(MediaMjpegClientId client_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    mjpeg_clients_.ReleaseWrite(client_id);
}

}  // namespace media_internal
}  // namespace live_stream
