#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_H_

#include "media/frame_source.h"
#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class MediaService;

using StreamFlvClientId = uint64_t;

struct StreamHubServiceOptions {
    uint32_t hls_segment_duration_ms = 1000;
    uint32_t hls_playlist_depth = 4;
    uint32_t max_flv_clients = 8;
};

struct StreamHubServiceDependencies {
    MediaService *media_service = nullptr;
};

struct StreamHlsEntry {
    uint64_t sequence = 0;
    int64_t duration_us = 0;
};

struct StreamHlsPlaylist {
    bool supported = false;
    uint32_t target_duration_sec = 0;
    uint64_t media_sequence = 0;
    std::vector<StreamHlsEntry> entries;
};

struct StreamSegment {
    bool found = false;
    uint64_t sequence = 0;
    int64_t duration_us = 0;
    std::string body;
};

struct StreamFlvBootstrap {
    bool supported = false;
    uint64_t config_generation = 0;
    std::string file_header;
    std::string sequence_header;
    std::string last_keyframe;
};

struct StreamHubServiceStats {
    bool enabled = false;
    uint64_t hls_segments_created = 0;
    uint32_t active_flv_clients = 0;
};

class IStreamFlvSink {
public:
    virtual ~IStreamFlvSink() = default;

    virtual bool OnFlvChunk(const uint8_t *data, size_t size) = 0;
};

class IStreamHubService {
public:
    virtual ~IStreamHubService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsHlsSupported(StreamId stream_id) const = 0;
    virtual bool IsFlvSupported(StreamId stream_id) const = 0;
    virtual StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
    virtual StreamSegment GetHlsSegment(StreamId stream_id,
                                        uint64_t sequence) const = 0;
    virtual StreamFlvBootstrap GetFlvBootstrap(StreamId stream_id) const = 0;
    virtual StreamFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    const std::shared_ptr<IStreamFlvSink> &sink) = 0;
    virtual bool DetachFlvClient(StreamFlvClientId client_id) = 0;
    virtual StreamHubServiceStats GetStats() const = 0;
};

std::unique_ptr<IStreamHubService>
CreateStreamHubService(const StreamHubServiceOptions &options,
                       const StreamHubServiceDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_H_
