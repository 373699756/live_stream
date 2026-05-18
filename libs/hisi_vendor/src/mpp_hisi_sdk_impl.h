#ifndef LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_

#include "hisi_vendor/mpp_hisi_sdk.h"

#include <atomic>
#include <pthread.h>
#include <thread>

namespace live_stream {
namespace hisisdk {

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
    bool venc_started_ = false;
    bool vpss_bound_venc_ = false;
    bool stream_started_ = false;
    pthread_t isp_thread_ = 0;

    std::thread main_stream_thread_;
    std::thread sub_stream_thread_;
    std::atomic<bool> stream_running_{false};

    EncodedFrameCallback frame_callback_ = nullptr;
    void* frame_callback_user_ = nullptr;
};

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_
