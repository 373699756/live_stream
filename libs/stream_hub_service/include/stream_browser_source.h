#ifndef LIVE_STREAM_STREAM_BROWSER_SOURCE_H_
#define LIVE_STREAM_STREAM_BROWSER_SOURCE_H_

#include "media/encoded_frame.h"
#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

constexpr size_t kMaxStreamFlvVideoTagSlices = 130;
constexpr size_t kMaxStreamFlvHeaderSliceBytes = 24;
constexpr size_t kMaxStreamFlvCachedVideoTags = 64;

struct StreamFlvVideoTagSlice {
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool media_payload = false;
};

struct StreamFlvVideoTagView {
    StreamFlvVideoTagSlice slices[kMaxStreamFlvVideoTagSlices];
    size_t slice_count = 0;
    uint32_t timestamp_ms = 0;
};

struct StreamFlvCachedVideoTagSlice {
    const uint8_t *media_data = nullptr;
    uint8_t header_data[kMaxStreamFlvHeaderSliceBytes] = {};
    size_t size = 0;
    bool media_payload = false;
};

struct StreamFlvCachedVideoTag {
    EncodedFrame frame;
    StreamFlvCachedVideoTagSlice slices[kMaxStreamFlvVideoTagSlices];
    size_t slice_count = 0;
    size_t total_size = 0;
    uint32_t timestamp_ms = 0;
};

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

struct StreamSegmentRef {
    bool found = false;
    uint64_t sequence = 0;
    int64_t duration_us = 0;
    VideoBuffer *body = nullptr;
};

struct StreamFlvStartData {
    bool supported = false;
    bool cached_gop_complete = false;
    uint64_t config_generation = 0;
    std::string file_header;
    std::string sequence_header;
    std::vector<StreamFlvCachedVideoTag> cached_video_tags;
};

inline void StreamFlvCachedVideoTagUnref(StreamFlvCachedVideoTag *tag) {
    if (tag == nullptr) {
        return;
    }
    EncodedFrameUnref(&tag->frame);
    *tag = StreamFlvCachedVideoTag{};
}

inline bool StreamFlvCachedVideoTagRefCopy(
    StreamFlvCachedVideoTag *target,
    const StreamFlvCachedVideoTag *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    EncodedFrame retained_frame;
    if (!EncodedFrameRefCopy(&retained_frame, &source->frame)) {
        return false;
    }
    StreamFlvCachedVideoTagUnref(target);
    *target = *source;
    target->frame = retained_frame;
    return true;
}

inline void StreamFlvStartDataUnref(StreamFlvStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (StreamFlvCachedVideoTag &tag : start_data->cached_video_tags) {
        StreamFlvCachedVideoTagUnref(&tag);
    }
    start_data->cached_video_tags.clear();
    start_data->file_header.clear();
    start_data->sequence_header.clear();
    start_data->supported = false;
    start_data->cached_gop_complete = false;
    start_data->config_generation = 0;
}

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
    virtual bool OnFlvVideoTag(const StreamFlvVideoTagView &tag,
                               const EncodedFrame &frame) = 0;
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
    virtual StreamSegmentRef GetHlsSegmentRef(StreamId stream_id,
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

inline StreamSegmentRef StreamSegmentRefCopy(
    const StreamSegmentRef *segment) {
    StreamSegmentRef ref;
    if (segment == nullptr || !segment->found || segment->body == nullptr) {
        return ref;
    }
    ref = *segment;
    ref.body = VideoBufferRetain(segment->body);
    if (ref.body == nullptr) {
        return StreamSegmentRef{};
    }
    return ref;
}

inline void StreamSegmentRefUnref(StreamSegmentRef *segment) {
    if (segment == nullptr) {
        return;
    }
    VideoBufferRelease(segment->body);
    *segment = StreamSegmentRef{};
}

}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_BROWSER_SOURCE_H_
