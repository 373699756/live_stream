#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_H_

#include "media/frame_subscription.h"
#include "media/stream_types.h"
#include "media_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

using StreamFlvClientId = uint64_t;
using StreamFrameSinkId = uint64_t;

struct StreamHubServiceOptions {
    uint32_t hls_segment_duration_ms = 1000;
    uint32_t hls_playlist_depth = 4;
    uint32_t max_flv_clients = 8;
    uint32_t max_frame_sinks = 8;
};

struct StreamHubServiceDependencies {
    IMediaService *media_service = nullptr;
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

struct StreamFlvStartData {
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
    uint32_t active_frame_sinks = 0;
};

struct StreamBrowserStatus {
    bool running = false;
    bool browser_codec = false;
    bool hls_supported = false;
    bool flv_supported = false;
    bool hls_ready = false;
    bool flv_ready = false;
    VideoCodec codec = VideoCodec::kH264;
    uint32_t hls_segment_count = 0;
    uint32_t flv_sequence_header_size = 0;
    uint32_t flv_last_keyframe_size = 0;
    uint32_t hls_current_segment_size = 0;
};

struct StreamFrameSinkOptions {
    StreamId stream_id = StreamId::kMain;
    bool require_key_frame_first = true;
    std::string sink_name;
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
    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
    virtual StreamSegment GetHlsSegment(StreamId stream_id,
                                        uint64_t sequence) const = 0;
    virtual StreamFlvStartData GetFlvStartData(StreamId stream_id) const = 0;
    virtual StreamBrowserStatus GetBrowserStatus(StreamId stream_id) const = 0;
    virtual StreamFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    bool wait_for_keyframe,
                    const std::shared_ptr<IStreamFlvSink> &sink) = 0;
    virtual bool DetachFlvClient(StreamFlvClientId client_id) = 0;
    virtual StreamFrameSinkId AttachFrameSink(
        const StreamFrameSinkOptions &options, IFrameSink *sink) = 0;
    virtual bool DetachFrameSink(StreamFrameSinkId sink_id) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
    virtual StreamHubServiceStats GetStats() const = 0;
};

std::unique_ptr<IStreamHubService>
CreateStreamHubService(const StreamHubServiceOptions &options,
                       const StreamHubServiceDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_H_
