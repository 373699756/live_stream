#ifndef LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_

#include "hisi_vendor/mpp_sdk.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace live_stream {
namespace hisisdk {

struct VencChannelState {
    StreamId stream_id = StreamId::kMain;
    int32_t venc_channel = -1;
    int32_t vpss_group = -1;
    int32_t vpss_channel = -1;
    Codec codec = Codec::kH264;
    VideoStreamConfig stream_config;
    bool created = false;
    bool bound_to_vpss = false;
    bool receiving = false;
    int fd = -1;
};

// Full definition of MppHisiSdk state shared across implementation files.
struct MppHisiSdkImpl {
    MediaPipelineConfig active_config_;
    bool has_active_config_ = false;

    bool system_initialized_ = false;
    bool system_cleanup_failed_ = false;
    bool vi_started_ = false;
    bool mipi_started_ = false;
    bool isp_started_ = false;
    bool vpss_started_ = false;
    bool dis_enabled_ = false;
    bool vi_bound_vpss_ = false;
    VencChannelState main_venc_;
    VencChannelState sub_venc_;
    std::thread isp_thread_;

    std::thread stream_thread_;
    std::atomic<bool> stream_running_{false};
    std::mutex control_mutex_;
    std::mutex snapshot_mutex_;

    MediaFrameCallback frame_callback_ = nullptr;
    void* frame_callback_user_ = nullptr;
};

// These stop helpers are called with control_mutex_ already held. They avoid
// public method re-entry so control_mutex_ can stay a plain mutex.
void StopViInput(MppHisiSdkImpl& impl, const MediaPipelineConfig& config);
void StopVpssGroup(MppHisiSdkImpl& impl, const MediaPipelineConfig& config);
void UnbindViVpssPipe(MppHisiSdkImpl& impl,
                      const MediaPipelineConfig& config);
void DestroyVencChannels(MppHisiSdkImpl& impl);
void UnbindVpssVencChannels(MppHisiSdkImpl& impl);
void StopVencStreamThread(MppHisiSdkImpl& impl);

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_MPP_HISI_SDK_IMPL_H_
