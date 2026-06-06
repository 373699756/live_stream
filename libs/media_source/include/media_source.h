#ifndef LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_
#define LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_

#include "media_frame.h"

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
using MediaFrameReaderId = uint64_t;

enum class MediaFrameReaderCloseReason {
    kNone = 0,
    kDetached,
    kStreamStopped,
    kCodecChanged,
    kTimestampReset,
    kCacheOverflow,
};

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

struct MediaFrameReaderOptions {
    StreamId stream_id = StreamId::kMain;
    bool keyframe_first = true;
    std::string reader_name;
};

struct MediaFrameReaderStartData {
    bool stream_running = false;
    bool gop_complete = false;
    uint64_t reader_generation = 0;
    MediaTrack track;
    std::vector<MediaFrame> gop_frames;
};

struct MediaFrameReaderFrame {
    MediaFrameReaderId reader_id = 0;
    uint64_t reader_generation = 0;
    bool starts_on_keyframe = false;
    MediaFrame frame;
};

struct MediaFrameReaderStatus {
    bool attached = false;
    StreamId stream_id = StreamId::kMain;
    uint64_t reader_generation = 0;
    MediaFrameReaderCloseReason close_reason =
        MediaFrameReaderCloseReason::kNone;
    bool waiting_for_keyframe = false;
    bool slow_reader = false;
    uint32_t pending_frames = 0;
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

inline void MediaFrameReaderStartDataUnref(
    MediaFrameReaderStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (MediaFrame &frame : start_data->gop_frames) {
        MediaFrameUnref(&frame);
    }
    start_data->gop_frames.clear();
    start_data->stream_running = false;
    start_data->gop_complete = false;
    start_data->reader_generation = 0;
    start_data->track = MediaTrack{};
}

inline void MediaFrameReaderFrameUnref(MediaFrameReaderFrame *reader_frame) {
    if (reader_frame == nullptr) {
        return;
    }
    MediaFrameUnref(&reader_frame->frame);
    reader_frame->reader_id = 0;
    reader_frame->reader_generation = 0;
    reader_frame->starts_on_keyframe = false;
}

struct MediaSourceStats {
    bool enabled = false;
    uint64_t hls_segments_created = 0;
    uint32_t active_flv_clients = 0;
    uint32_t active_mjpeg_clients = 0;
    uint32_t active_frame_sinks = 0;
    uint32_t active_frame_readers = 0;
    uint32_t cached_frames = 0;
    uint32_t cached_bytes = 0;
    uint32_t slow_reader_count = 0;
    int64_t main_last_frame_timestamp_us = 0;
    int64_t sub_last_frame_timestamp_us = 0;
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
    virtual MediaFrameReaderId AttachFrameReader(
        const MediaFrameReaderOptions &options) = 0;
    virtual bool DetachFrameReader(MediaFrameReaderId reader_id,
                                   MediaFrameReaderCloseReason reason) = 0;
    virtual MediaFrameReaderStatus GetFrameReaderStatus(
        MediaFrameReaderId reader_id) const = 0;
    virtual MediaFrameReaderStartData GetFrameReaderStartData(
        MediaFrameReaderId reader_id) const = 0;
    virtual bool PopFrameReaderFrame(MediaFrameReaderId reader_id,
                                     MediaFrameReaderFrame *frame) = 0;
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

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_MEDIA_SOURCE_H_
