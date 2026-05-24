#ifndef LIVE_STREAM_STREAM_BROWSER_SOURCE_H_
#define LIVE_STREAM_STREAM_BROWSER_SOURCE_H_

#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

struct EncodedFrame;

using StreamFlvClientId = uint64_t;
using StreamMjpegClientId = uint64_t;

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
    uint32_t active_mjpeg_clients = 0;
    uint32_t active_frame_sinks = 0;
};

struct StreamBrowserStatus {
    bool running = false;
    bool browser_codec = false;
    bool hls_ready = false;
    bool flv_ready = false;
    bool mjpeg_ready = false;
    VideoCodec codec = VideoCodec::kH264;
    uint32_t hls_segment_count = 0;
    uint32_t flv_sequence_header_size = 0;
    uint32_t flv_last_keyframe_size = 0;
    uint32_t hls_current_segment_size = 0;
    bool hls_supported = false;
    bool flv_supported = false;
    bool mjpeg_supported = false;
};

class IStreamFlvSink {
public:
    virtual ~IStreamFlvSink() = default;

    virtual bool OnFlvChunk(const uint8_t *data, size_t size) = 0;
};

class IStreamMjpegSink {
public:
    virtual ~IStreamMjpegSink() = default;

    virtual bool OnMjpegFrame(const EncodedFrame &frame) = 0;
};

class IStreamBrowserSource {
public:
    virtual ~IStreamBrowserSource() = default;

    virtual bool IsHlsSupported(StreamId stream_id) const = 0;
    virtual bool IsFlvSupported(StreamId stream_id) const = 0;
    virtual bool IsMjpegSupported(StreamId stream_id) const = 0;
    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
    virtual StreamSegment GetHlsSegment(StreamId stream_id,
                                        uint64_t sequence) const = 0;
    virtual StreamFlvStartData GetFlvStartData(StreamId stream_id) const = 0;
    virtual StreamBrowserStatus GetBrowserStatus(StreamId stream_id) const = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
    virtual StreamHubServiceStats GetStats() const = 0;
};

class IStreamFlvSource {
public:
    virtual ~IStreamFlvSource() = default;

    virtual StreamFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    bool wait_for_keyframe, IStreamFlvSink *sink) = 0;
    virtual bool DetachFlvClient(StreamFlvClientId client_id) = 0;
};

class IStreamMjpegSource {
public:
    virtual ~IStreamMjpegSource() = default;

    virtual StreamMjpegClientId
    AttachMjpegClient(StreamId stream_id, IStreamMjpegSink *sink) = 0;
    virtual bool DetachMjpegClient(StreamMjpegClientId client_id) = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_BROWSER_SOURCE_H_
