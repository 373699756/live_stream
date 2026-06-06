#ifndef LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_
#define LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_

#include "media/encoded_frame.h"
#include "media/frame_attach.h"
#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

constexpr size_t kMaxMediaFlvVideoTagSlices = 130;
constexpr size_t kMaxMediaFlvHeaderSliceBytes = 24;
constexpr size_t kMaxMediaFlvCachedVideoTags = 128;

struct MediaFlvVideoTagSlice {
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool media_payload = false;
};

struct MediaFlvVideoTagView {
    MediaFlvVideoTagSlice slices[kMaxMediaFlvVideoTagSlices];
    size_t slice_count = 0;
    uint32_t timestamp_ms = 0;
};

struct MediaFlvCachedVideoTagSlice {
    const uint8_t *media_data = nullptr;
    uint8_t header_data[kMaxMediaFlvHeaderSliceBytes] = {};
    size_t size = 0;
    bool media_payload = false;
};

struct MediaFlvCachedVideoTag {
    EncodedFrame frame;
    MediaFlvCachedVideoTagSlice slices[kMaxMediaFlvVideoTagSlices];
    size_t slice_count = 0;
    size_t total_size = 0;
    uint32_t timestamp_ms = 0;
};

using MediaFlvClientId = uint64_t;
using MediaMjpegClientId = uint64_t;

struct MediaHlsEntry {
    uint64_t sequence = 0;
    int64_t duration_us = 0;
};

struct MediaHlsPlaylist {
    bool supported = false;
    uint32_t target_duration_sec = 0;
    uint64_t media_sequence = 0;
    std::vector<MediaHlsEntry> entries;
};

struct MediaSegmentRef {
    bool found = false;
    uint64_t sequence = 0;
    int64_t duration_us = 0;
    VideoBuffer *body = nullptr;
};

struct MediaFlvStartData {
    bool supported = false;
    bool cached_gop_complete = false;
    uint64_t config_generation = 0;
    std::string file_header;
    std::string sequence_header;
    std::vector<MediaFlvCachedVideoTag> cached_video_tags;
};

inline void MediaFlvCachedVideoTagUnref(MediaFlvCachedVideoTag *tag) {
    if (tag == nullptr) {
        return;
    }
    EncodedFrameUnref(&tag->frame);
    *tag = MediaFlvCachedVideoTag{};
}

inline bool MediaFlvCachedVideoTagRefCopy(
    MediaFlvCachedVideoTag *target,
    const MediaFlvCachedVideoTag *source) {
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
    MediaFlvCachedVideoTagUnref(target);
    *target = *source;
    target->frame = retained_frame;
    return true;
}

inline void MediaFlvStartDataUnref(MediaFlvStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (MediaFlvCachedVideoTag &tag : start_data->cached_video_tags) {
        MediaFlvCachedVideoTagUnref(&tag);
    }
    start_data->cached_video_tags.clear();
    start_data->file_header.clear();
    start_data->sequence_header.clear();
    start_data->supported = false;
    start_data->cached_gop_complete = false;
    start_data->config_generation = 0;
}

struct MediaSourceStats {
    bool enabled = false;
    uint64_t hls_segments_created = 0;
    uint32_t active_flv_clients = 0;
    uint32_t active_mjpeg_clients = 0;
    uint32_t active_frame_sinks = 0;
};

struct MediaSourceStatus {
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

class IMediaFlvSink {
public:
    virtual ~IMediaFlvSink() = default;

    virtual bool OnFlvChunk(const uint8_t *data, size_t size) = 0;
    virtual bool OnFlvVideoTag(const MediaFlvVideoTagView &tag,
                               const EncodedFrame &frame) = 0;
};

class IMediaMjpegSink {
public:
    virtual ~IMediaMjpegSink() = default;

    virtual bool OnMjpegFrame(const EncodedFrame &frame) = 0;
};

class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    virtual bool IsHlsSupported(StreamId stream_id) const = 0;
    virtual bool IsFlvSupported(StreamId stream_id) const = 0;
    virtual bool IsMjpegSupported(StreamId stream_id) const = 0;
    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
    virtual MediaSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                             uint64_t sequence) const = 0;
    virtual MediaFlvStartData GetFlvStartData(StreamId stream_id) const = 0;
    virtual MediaSourceStatus GetBrowserStatus(StreamId stream_id) const = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
    virtual MediaSourceStats GetStats() const = 0;
};

class IMediaFlvSource {
public:
    virtual ~IMediaFlvSource() = default;

    virtual MediaFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    bool wait_for_keyframe, IMediaFlvSink *sink) = 0;
    virtual bool DetachFlvClient(MediaFlvClientId client_id) = 0;
};

class IMediaMjpegSource {
public:
    virtual ~IMediaMjpegSource() = default;

    virtual MediaMjpegClientId
    AttachMjpegClient(StreamId stream_id, IMediaMjpegSink *sink) = 0;
    virtual bool DetachMjpegClient(MediaMjpegClientId client_id) = 0;
};

class IMediaFrameSource {
public:
    virtual ~IMediaFrameSource() = default;

    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual FrameAttachId AttachFrameSink(
        const FrameAttachOptions &options, IFrameSink *sink) = 0;
    virtual bool DetachFrameSink(FrameAttachId sink_id) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
};

inline MediaSegmentRef MediaSegmentRefCopy(
    const MediaSegmentRef *segment) {
    MediaSegmentRef ref;
    if (segment == nullptr || !segment->found || segment->body == nullptr) {
        return ref;
    }
    ref = *segment;
    ref.body = VideoBufferRef(segment->body);
    if (ref.body == nullptr) {
        return MediaSegmentRef{};
    }
    return ref;
}

inline void MediaSegmentRefUnref(MediaSegmentRef *segment) {
    if (segment == nullptr) {
        return;
    }
    VideoBufferUnref(segment->body);
    *segment = MediaSegmentRef{};
}

// Temporary aliases keep existing protocol modules buildable while their
// implementation is migrated to the MediaSource naming.
constexpr size_t kMaxStreamFlvVideoTagSlices = kMaxMediaFlvVideoTagSlices;
constexpr size_t kMaxStreamFlvHeaderSliceBytes = kMaxMediaFlvHeaderSliceBytes;
constexpr size_t kMaxStreamFlvCachedVideoTags = kMaxMediaFlvCachedVideoTags;
using StreamFlvVideoTagSlice = MediaFlvVideoTagSlice;
using StreamFlvVideoTagView = MediaFlvVideoTagView;
using StreamFlvCachedVideoTagSlice = MediaFlvCachedVideoTagSlice;
using StreamFlvCachedVideoTag = MediaFlvCachedVideoTag;
using StreamFlvClientId = MediaFlvClientId;
using StreamMjpegClientId = MediaMjpegClientId;
using StreamHlsEntry = MediaHlsEntry;
using StreamHlsPlaylist = MediaHlsPlaylist;
using StreamSegmentRef = MediaSegmentRef;
using StreamFlvStartData = MediaFlvStartData;
using StreamHubServiceStats = MediaSourceStats;
using StreamBrowserStatus = MediaSourceStatus;
using IStreamFlvSink = IMediaFlvSink;
using IStreamMjpegSink = IMediaMjpegSink;
using IStreamBrowserSource = IMediaSource;
using IStreamFlvSource = IMediaFlvSource;
using IStreamMjpegSource = IMediaMjpegSource;
using IStreamFrameSource = IMediaFrameSource;

inline void StreamFlvCachedVideoTagUnref(StreamFlvCachedVideoTag *tag) {
    MediaFlvCachedVideoTagUnref(tag);
}

inline bool StreamFlvCachedVideoTagRefCopy(
    StreamFlvCachedVideoTag *target,
    const StreamFlvCachedVideoTag *source) {
    return MediaFlvCachedVideoTagRefCopy(target, source);
}

inline void StreamFlvStartDataUnref(StreamFlvStartData *start_data) {
    MediaFlvStartDataUnref(start_data);
}

inline StreamSegmentRef StreamSegmentRefCopy(
    const StreamSegmentRef *segment) {
    return MediaSegmentRefCopy(segment);
}

inline void StreamSegmentRefUnref(StreamSegmentRef *segment) {
    MediaSegmentRefUnref(segment);
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_
