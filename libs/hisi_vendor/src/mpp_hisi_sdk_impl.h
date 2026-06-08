#ifndef LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_

#include "hisi_vendor/mpp_hisi_sdk.h"

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <thread>

namespace live_stream {
namespace hisisdk {

struct VencChannelRuntime {
    StreamId stream_id = StreamId::kMain;
    int32_t venc_channel = -1;
    int32_t vpss_group = -1;
    int32_t vpss_channel = -1;
    VideoCodec codec = VideoCodec::kH264;
    VideoStreamConfig stream_config;
    bool created = false;
    bool bound_to_vpss = false;
    bool receiving = false;
    int fd = -1;
};

// Full definition of MppHisiSdk::Impl – shared across all translation units.
struct MppHisiSdk::Impl {
    MediaPipelineConfig active_config_;
    bool has_active_config_ = false;

    bool system_initialized_ = false;
    bool vi_started_ = false;
    bool mipi_started_ = false;
    bool isp_started_ = false;
    bool vpss_started_ = false;
    bool vi_bound_vpss_ = false;
    VencChannelRuntime main_venc_;
    VencChannelRuntime sub_venc_;
    pthread_t isp_thread_ = 0;

    std::thread stream_thread_;
    std::atomic<bool> stream_running_{false};
    std::recursive_mutex control_mutex_;
    std::mutex snapshot_mutex_;
    bool snapshot_in_progress_ = false;

    EncodedFrameCallback frame_callback_ = nullptr;
    void* frame_callback_user_ = nullptr;
};

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_
