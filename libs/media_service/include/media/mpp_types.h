#ifndef LIVE_STREAM_MEDIA_MPP_TYPES_H_
#define LIVE_STREAM_MEDIA_MPP_TYPES_H_

#include <cstdint>

namespace live_stream {

enum class MppModule {
    kVi = 0,
    kVpss,
    kVenc,
    kVo,
};

struct MppChannel {
    MppModule module = MppModule::kVi;
    int32_t device = 0;
    int32_t channel = 0;
};

struct MediaChannels {
    MppChannel vi;
    MppChannel vpss;
    MppChannel sub_vpss;
    MppChannel venc;
    MppChannel sub_venc;
    int32_t video_pipe = 0;
    int32_t snap_pipe = 2;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MPP_TYPES_H_
