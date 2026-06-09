#ifndef LIVE_STREAM_MEDIA_PIPELINE_CONFIG_H_
#define LIVE_STREAM_MEDIA_PIPELINE_CONFIG_H_

#include "media/stream_types.h"
#include "media/media_capabilities.h"

#include <cstdint>
#include <vector>

namespace live_stream {

struct VideoSize {
    uint32_t width = 1920;
    uint32_t height = 1080;
};

struct FrameRate {
    int32_t source_fps = 30;
    int32_t target_fps = 30;
};

struct VideoRoiRegion {
    bool enabled = false;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t qp = -6;
    bool absolute_qp = false;
};

struct VideoRoiConfig {
    bool enabled = false;
    std::vector<VideoRoiRegion> regions;
};

struct VideoStreamConfig {
    StreamId stream_id = StreamId::kMain;
    bool enabled = true;
    VideoCodec codec = VideoCodec::kH264;
    VideoSize size;
    FrameRate frame_rate;
    uint32_t bitrate_kbps = 2048;
    uint32_t gop = 30;
    RateControlMode rc_mode = RateControlMode::kCbr;
    GopMode gop_mode = GopMode::kNormalP;
    VideoRoiConfig roi;
};

struct MediaPipelineConfig {
    int32_t sensor_id = 0;
    int32_t video_pipe = 0;
    int32_t snap_pipe = 2;
    int32_t vi_channel = 0;
    int32_t vpss_group = 0;
    int32_t vpss_channel = 0;
    int32_t sub_vpss_channel = 1;
    int32_t venc_channel = 0;
    int32_t sub_venc_channel = 1;
    uint32_t vb_block_count = 10;
    VideoStreamConfig main_stream;
    VideoStreamConfig sub_stream{StreamId::kSub,
                                 true,
                                 VideoCodec::kH264,
                                 VideoSize{1280, 720},
                                 FrameRate{30, 30},
                                 3072,
                                 60,
                                 RateControlMode::kCbr,
                                 GopMode::kNormalP,
                                 VideoRoiConfig{}};
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_PIPELINE_CONFIG_H_
