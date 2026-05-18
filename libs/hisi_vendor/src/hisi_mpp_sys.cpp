#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cstring>

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

uint64_t Yuv420BlockSize(const VideoSize& size) {
    constexpr uint32_t kDefaultAlign = 32;
    const uint64_t stride = AlignUp(size.width, kDefaultAlign);
    const uint64_t height = AlignUp(size.height, 2);
    return stride * height * 3 / 2;
}

// ─── Video buffer configuration ─────────────────────────────────
bool ConfigureVideoBuffer(const MediaPipelineConfig& config) {
    VB_CONFIG_S vb_conf{};
    (void)std::memset(&vb_conf, 0, sizeof(vb_conf));

    // Main stream VB pool
    vb_conf.astCommPool[0].u64BlkSize =
        Yuv420BlockSize(config.main_stream.size);
    vb_conf.astCommPool[0].u32BlkCnt = config.vb_block_count;
    vb_conf.astCommPool[0].enRemapMode = VB_REMAP_MODE_CACHED;

    // Sub-stream VB pool (if enabled)
    if (config.sub_stream.enabled) {
        vb_conf.astCommPool[1].u64BlkSize =
            Yuv420BlockSize(config.sub_stream.size);
        vb_conf.astCommPool[1].u32BlkCnt = 4;
        vb_conf.astCommPool[1].enRemapMode = VB_REMAP_MODE_CACHED;
    }

    vb_conf.u32MaxPoolCnt = 4;

    HISI_CHECK(HI_MPI_VB_SetConfig(&vb_conf));
    HISI_CHECK(HI_MPI_VB_Init());
    const HI_S32 sys_status = HI_MPI_SYS_Init();
    if (sys_status != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor", "HI_MPI_SYS_Init failed: 0x%08x",
                        sys_status);
        HI_MPI_VB_Exit();
        return false;
    }

    return true;
}

bool ConfigureViVpssMode(const MediaPipelineConfig& config) {
    VI_VPSS_MODE_S vi_vpss_mode{};
    HISI_CHECK(HI_MPI_SYS_GetVIVPSSMode(&vi_vpss_mode));

    if (config.video_pipe >= 0 && config.video_pipe < VI_MAX_PIPE_NUM) {
        vi_vpss_mode.aenMode[config.video_pipe] = VI_OFFLINE_VPSS_OFFLINE;
    }
    if (config.snap_pipe >= 0 && config.snap_pipe < VI_MAX_PIPE_NUM) {
        vi_vpss_mode.aenMode[config.snap_pipe] = VI_OFFLINE_VPSS_OFFLINE;
    }

    HISI_CHECK(HI_MPI_SYS_SetVIVPSSMode(&vi_vpss_mode));
    return true;
}

}  // anonymous namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

// ====================================================================
// InitSystem / DeinitSystem
// ====================================================================
bool MppHisiSdk::InitSystem(const MediaPipelineConfig& config) {
    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

    if (impl_->system_initialized_) {
        return true;
    }

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_VERSION_S version{};
    HISI_CHECK(HI_MPI_SYS_GetVersion(&version));
    INFRA_LOG_INFO("hisi_vendor", "HISI MPP version: %s", version.aVersion);

    if (!ConfigureVideoBuffer(config)) {
        INFRA_LOG_ERROR("hisi_vendor", "ConfigureVideoBuffer failed");
        return false;
    }
    if (!ConfigureViVpssMode(config)) {
        INFRA_LOG_ERROR("hisi_vendor", "ConfigureViVpssMode failed");
        HI_MPI_SYS_Exit();
        HI_MPI_VB_Exit();
        return false;
    }

    impl_->system_initialized_ = true;
    return true;

#else
    (void)config;
    impl_->system_initialized_ = true;
    return true;
#endif
}

void MppHisiSdk::DeinitSystem() {
    if (!impl_->system_initialized_) {
        return;
    }

    const MediaPipelineConfig& config = impl_->active_config_;

    StopVencStream(config);
    UnbindVpssVenc(config);
    StopVenc(config);
    UnbindViVpss(config);
    StopVpss(config);
    StopVi(config);

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    HI_MPI_SYS_Exit();
    HI_MPI_VB_Exit();
#endif

    impl_->system_initialized_ = false;
    impl_->has_active_config_ = false;
}

}  // namespace hisisdk
}  // namespace live_stream
