#ifndef LIVE_STREAM_MEDIA_SOURCE_MEDIA_FRAME_H_
#define LIVE_STREAM_MEDIA_SOURCE_MEDIA_FRAME_H_

#include "media/encoded_frame.h"

#include <cstdint>
#include <string>

namespace live_stream {

enum class MediaTrackType {
    kVideo = 0,
};

struct MediaTrack {
    MediaTrackType track_type = MediaTrackType::kVideo;
    StreamId stream_id = StreamId::kMain;
    VideoCodec codec = VideoCodec::kH264;
    // 当前只做视频预览链路，RTP/HLS 时间基统一按视频 90kHz 时钟输出。
    uint32_t clock_rate = 90000;
    // codec_generation 在 codec、参数集、时间戳或缓存重置时递增，用于调用方
    // 判断旧 reader/缓存是否还属于当前可播放代际。
    uint64_t codec_generation = 0;
    // 参数集由 media_source 持有副本，reader/协议模块可在原始帧释放后继续
    // 构造 SDP、sequence header 或关键帧前置参数集。
    std::string vps;
    std::string sps;
    std::string pps;
    bool ready = false;
};

struct MediaFrame {
    // 协议热路径的统一帧对象。encoded_frame 持有底层 VideoBuffer 引用，
    // 不复制大块视频 payload；析构或丢弃前必须调用 MediaFrameUnref。
    EncodedFrame encoded_frame;
    MediaTrackType track_type = MediaTrackType::kVideo;
    StreamId stream_id = StreamId::kMain;
    VideoCodec codec = VideoCodec::kH264;
    bool key_frame = false;
    // dts_us/pts_us 是 media_source 修正后的相对时间戳，不再是设备 SDK
    // 原始时间戳；RTSP/WebRTC/HLS/FLV 都应使用这组时间。
    int64_t dts_us = 0;
    int64_t pts_us = 0;
    int64_t duration_us = 0;
};

inline void MediaFrameInit(MediaFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    EncodedFrameInit(&frame->encoded_frame);
    frame->track_type = MediaTrackType::kVideo;
    frame->stream_id = StreamId::kMain;
    frame->codec = VideoCodec::kH264;
    frame->key_frame = false;
    frame->dts_us = 0;
    frame->pts_us = 0;
    frame->duration_us = 0;
}

inline void MediaFrameUnref(MediaFrame *frame) {
    if (frame == nullptr) {
        return;
    }
    EncodedFrameUnref(&frame->encoded_frame);
    frame->track_type = MediaTrackType::kVideo;
    frame->stream_id = StreamId::kMain;
    frame->codec = VideoCodec::kH264;
    frame->key_frame = false;
    frame->dts_us = 0;
    frame->pts_us = 0;
    frame->duration_us = 0;
}

inline bool MediaFrameSetEncodedFrame(MediaFrame *target,
                                      const EncodedFrame *source,
                                      MediaTrackType track_type,
                                      bool key_frame,
                                      int64_t duration_us) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    EncodedFrame retained_frame;
    if (!EncodedFrameRefCopy(&retained_frame, source)) {
        return false;
    }
    MediaFrameUnref(target);
    target->encoded_frame = retained_frame;
    target->track_type = track_type;
    target->stream_id = source->stream_id;
    target->codec = source->codec;
    target->key_frame = key_frame;
    target->dts_us = source->dts_us;
    target->pts_us = source->pts_us;
    target->duration_us = duration_us;
    return true;
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
    target->stream_id = source->stream_id;
    target->codec = source->codec;
    target->key_frame = source->key_frame;
    target->dts_us = source->dts_us;
    target->pts_us = source->pts_us;
    target->duration_us = source->duration_us;
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
    target->stream_id = source->stream_id;
    target->codec = source->codec;
    target->key_frame = source->key_frame;
    target->dts_us = source->dts_us;
    target->pts_us = source->pts_us;
    target->duration_us = source->duration_us;
    source->track_type = MediaTrackType::kVideo;
    source->stream_id = StreamId::kMain;
    source->codec = VideoCodec::kH264;
    source->key_frame = false;
    source->dts_us = 0;
    source->pts_us = 0;
    source->duration_us = 0;
    return EncodedFrameMove(&target->encoded_frame, &source->encoded_frame);
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_MEDIA_FRAME_H_
