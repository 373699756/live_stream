#include "hisi_vendor/mpp_sdk.h"
#include "hisi_mpp_sdk.h"
#include "hisi_mpp_system_cleanup.h"
#include "hisi_mpp_vb_config.h"
#include "mpp_hisi_sdk_impl.h"

#include "infra/log.h"

namespace live_stream {
namespace hisisdk {

namespace {

bool ConfigureViVpssMode(const MediaPipelineConfig& config) {
    VI_VPSS_MODE_S vi_vpss_mode{};
    HI_S32 status = HI_MPI_SYS_GetVIVPSSMode(&vi_vpss_mode);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_SYS_GetVIVPSSMode video_pipe=%d snap_pipe=%d failed: "
              "0x%08x",
              config.video_pipe, config.snap_pipe, status);
        return false;
    }

    if (config.video_pipe >= 0 && config.video_pipe < VI_MAX_PIPE_NUM) {
        vi_vpss_mode.aenMode[config.video_pipe] = VI_OFFLINE_VPSS_OFFLINE;
    }
    if (config.snap_pipe >= 0 && config.snap_pipe < VI_MAX_PIPE_NUM) {
        vi_vpss_mode.aenMode[config.snap_pipe] = VI_OFFLINE_VPSS_OFFLINE;
    }

    status = HI_MPI_SYS_SetVIVPSSMode(&vi_vpss_mode);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_SYS_SetVIVPSSMode video_pipe=%d snap_pipe=%d failed: "
              "0x%08x",
              config.video_pipe, config.snap_pipe, status);
        return false;
    }
    return true;
}

}  // anonymous namespace

// ====================================================================
// InitSystem / DeinitSystem
// ====================================================================
bool MppHisiSdk::InitSystem(const MediaPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (impl_->system_cleanup_failed_) {
        Error("hisi_vendor",
              "HISI MPP cleanup is still failed; refusing InitSystem");
        return false;
    }
    if (impl_->system_initialized_) {
        return true;
    }

    MPP_VERSION_S version{};
    const HI_S32 version_status = HI_MPI_SYS_GetVersion(&version);
    if (version_status != HI_SUCCESS) {
        Error("hisi_vendor", "HI_MPI_SYS_GetVersion failed: 0x%08x",
              version_status);
        return false;
    }
    bool mpp_cleanup_failed = false;
    if (!mpp_vb_config::ConfigureFrameBuffer(
            config, &mpp_cleanup_failed)) {
        if (mpp_cleanup_failed) {
            impl_->system_cleanup_failed_ = true;
            Error("hisi_vendor",
                  "ConfigureFrameBuffer failed after HISI MPP cleanup");
        } else {
            Error("hisi_vendor", "ConfigureFrameBuffer failed");
        }
        return false;
    }

    if (!ConfigureViVpssMode(config)) {
        Error("hisi_vendor", "ConfigureViVpssMode failed");
        if (!mpp_system_cleanup::ExitMppSystem(
                true, mpp_system_cleanup::kMppExitRetryLimit,
                mpp_system_cleanup::MppExitBusyLog::kWarn)) {
            impl_->system_cleanup_failed_ = true;
        }
        return false;
    }

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;
    impl_->system_initialized_ = true;
    impl_->system_cleanup_failed_ = false;
    Info("hisi_vendor",
         "HISI MPP system initialized version=%s video_pipe=%d snap_pipe=%d",
         version.aVersion, config.video_pipe, config.snap_pipe);
    return true;
}

bool MppHisiSdk::DeinitSystem() {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (!impl_->system_initialized_ && !impl_->system_cleanup_failed_) {
        return true;
    }

    const MediaPipelineConfig& config = impl_->active_config_;

    StopVencStreamThread(*impl_);
    UnbindVpssVencChannels(*impl_);
    DestroyVencChannels(*impl_);
    UnbindViVpssPipe(*impl_, config);
    StopVpssGroup(*impl_, config);
    StopViInput(*impl_, config);

    bool cleanup_ok = mpp_system_cleanup::ExitMppSystem(
        true, mpp_system_cleanup::kMppExitRetryLimit,
        mpp_system_cleanup::MppExitBusyLog::kWarn);
    if (!cleanup_ok) {
        mpp_system_cleanup::ForceCleanupPipelineChannels(config, true);
        cleanup_ok = mpp_system_cleanup::ExitMppSystem(
            true, mpp_system_cleanup::kMppExitRetryLimit,
            mpp_system_cleanup::MppExitBusyLog::kWarn);
    }
    if (!cleanup_ok) {
        Error("hisi_vendor",
              "HISI MPP cleanup failed; keeping system initialized state");
        impl_->system_cleanup_failed_ = true;
        return false;
    }

    impl_->system_initialized_ = false;
    impl_->system_cleanup_failed_ = false;
    impl_->dis_enabled_ = false;
    impl_->has_active_config_ = false;
    return true;
}

}  // namespace hisisdk
}  // namespace live_stream
