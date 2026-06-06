#ifndef LIVE_STREAM_MEDIA_SOURCE_MEDIA_FRAME_H_
#define LIVE_STREAM_MEDIA_SOURCE_MEDIA_FRAME_H_

#include "media/encoded_frame.h"

#include <cstdint>

namespace live_stream {

enum class MediaTrackType {
    kVideo = 0,
};

struct MediaTrack {
    MediaTrackType track_type = MediaTrackType::kVideo;
    StreamId stream_id = StreamId::kMain;
    VideoCodec codec = VideoCodec::kH264;
    bool ready = false;
};

struct MediaFrame {
    EncodedFrame encoded_frame;
    MediaTrackType track_type = MediaTrackType::kVideo;
    bool key_frame = false;
};

inline void MediaFrameInit(MediaFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    EncodedFrameInit(&frame->encoded_frame);
    frame->track_type = MediaTrackType::kVideo;
    frame->key_frame = false;
}

inline void MediaFrameUnref(MediaFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    EncodedFrameUnref(&frame->encoded_frame);
    frame->track_type = MediaTrackType::kVideo;
    frame->key_frame = false;
}

inline bool MediaFrameRefCopy(MediaFrame *target,
                              const MediaFrame *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    EncodedFrame retained_frame;
    if (!EncodedFrameRefCopy(&retained_frame, &source->encoded_frame)) {
        return false;
    }
    MediaFrameUnref(target);
    target->encoded_frame = retained_frame;
    target->track_type = source->track_type;
    target->key_frame = source->key_frame;
    return true;
}

inline bool MediaFrameMove(MediaFrame *target, MediaFrame *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    MediaFrameUnref(target);
    target->track_type = source->track_type;
    target->key_frame = source->key_frame;
    source->track_type = MediaTrackType::kVideo;
    source->key_frame = false;
    return EncodedFrameMove(&target->encoded_frame, &source->encoded_frame);
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_MEDIA_FRAME_H_
