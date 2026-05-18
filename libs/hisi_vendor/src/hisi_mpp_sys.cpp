#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cstring>
#include <unistd.h>

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

constexpr int kMppExitRetryCount = 20;
constexpr useconds_t kMppExitRetryDelayUs = 100 * 1000;
constexpr int kMaxCleanupVencChannels = 4;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

uint64_t Yuv420BlockSize(const VideoSize& size) {
    constexpr uint32_t kDefaultAlign = 32;
    const uint64_t stride = AlignUp(size.width, kDefaultAlign);
    const uint64_t height = AlignUp(size.height, 2);
    return stride * height * 3 / 2;
}

bool IsVbExitDone(HI_S32 status) {
    return status == HI_SUCCESS || status == HI_ERR_VB_NOTREADY;
}

bool IsExpectedCleanupStatus(HI_S32 status) {
    return status == HI_SUCCESS || status == HI_ERR_SYS_NOTREADY ||
           status == HI_ERR_VB_NOTREADY || status == HI_ERR_VB_UNEXIST;
}

void LogCleanupStatus(const char* step, HI_S32 status, bool log_errors) {
    if (!log_errors || IsExpectedCleanupStatus(status)) {
        return;
    }
    INFRA_LOG_WARN("hisi_vendor", "%s during cleanup returned 0x%08x", step,
                   status);
}

HI_S32 ExitMppSystemOnce(bool log_errors) {
    HI_S32 status = HI_MPI_SYS_Exit();
    LogCleanupStatus("HI_MPI_SYS_Exit", status, log_errors);

    status = HI_MPI_VB_ExitModCommPool(VB_UID_VDEC);
    LogCleanupStatus("HI_MPI_VB_ExitModCommPool(VB_UID_VDEC)", status,
                     log_errors);

    status = HI_MPI_VB_Exit();
    LogCleanupStatus("HI_MPI_VB_Exit", status, log_errors);
    return status;
}

bool ExitMppSystem(bool log_errors, int retry_count) {
    HI_S32 status = HI_SUCCESS;
    for (int attempt = 0; attempt <= retry_count; ++attempt) {
        status = ExitMppSystemOnce(log_errors);
        if (IsVbExitDone(status)) {
            return true;
        }
        if (status != HI_ERR_VB_BUSY || attempt == retry_count) {
            break;
        }
        if (log_errors) {
            INFRA_LOG_WARN(
                "hisi_vendor",
                "HI_MPI_VB_Exit still busy, retry %d/%d after %u us",
                attempt + 1, retry_count,
                static_cast<unsigned>(kMppExitRetryDelayUs));
        }
        usleep(kMppExitRetryDelayUs);
    }
    return false;
}

bool IsValidVencChannel(int32_t channel) {
    return channel >= 0 && channel < VENC_MAX_CHN_NUM;
}

void AddCleanupVencChannel(int32_t channel, int32_t* channels, int* count) {
    if (!IsValidVencChannel(channel) || channels == nullptr ||
        count == nullptr || *count >= kMaxCleanupVencChannels) {
        return;
    }
    for (int i = 0; i < *count; ++i) {
        if (channels[i] == channel) {
            return;
        }
    }
    channels[*count] = channel;
    ++(*count);
}

void UnbindVpssVencDirect(int32_t vpss_group, int32_t vpss_channel,
                          int32_t venc_channel) {
    if (vpss_group < 0 || vpss_channel < 0 ||
        !IsValidVencChannel(venc_channel)) {
        return;
    }

    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = vpss_group;
    src.s32ChnId = vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = venc_channel;

    (void)HI_MPI_SYS_UnBind(&src, &dst);
}

void UnbindViVpssDirect(const MediaPipelineConfig& config) {
    if (config.video_pipe < 0 || config.vi_channel < 0 ||
        config.vpss_group < 0) {
        return;
    }

    MPP_CHN_S src{};
    src.enModId = HI_ID_VI;
    src.s32DevId = config.video_pipe;
    src.s32ChnId = config.vi_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VPSS;
    dst.s32DevId = config.vpss_group;
    dst.s32ChnId = 0;

    (void)HI_MPI_SYS_UnBind(&src, &dst);
}

void DestroyVencDirect(int32_t channel) {
    if (!IsValidVencChannel(channel)) {
        return;
    }
    const VENC_CHN venc = static_cast<VENC_CHN>(channel);
    (void)HI_MPI_VENC_StopRecvFrame(venc);
    (void)HI_MPI_VENC_DestroyChn(venc);
}

void ForceCleanupPipelineResources(const MediaPipelineConfig& config) {
    INFRA_LOG_WARN(
        "hisi_vendor",
        "stale HISI MPP resources detected, force cleanup known channels");

    const int32_t jpeg_venc_channel = SnapshotConfig{}.jpeg_venc_channel;
    UnbindVpssVencDirect(config.vpss_group, config.vpss_channel,
                         config.venc_channel);
    UnbindVpssVencDirect(config.vpss_group, config.vpss_channel,
                         jpeg_venc_channel);
    if (config.sub_stream.enabled) {
        UnbindVpssVencDirect(config.vpss_group, config.sub_vpss_channel,
                             config.sub_venc_channel);
        UnbindVpssVencDirect(config.vpss_group, config.sub_vpss_channel,
                             jpeg_venc_channel);
    }

    int32_t venc_channels[kMaxCleanupVencChannels] = {};
    int venc_channel_count = 0;
    AddCleanupVencChannel(config.venc_channel, venc_channels,
                          &venc_channel_count);
    AddCleanupVencChannel(config.sub_venc_channel, venc_channels,
                          &venc_channel_count);
    AddCleanupVencChannel(jpeg_venc_channel, venc_channels,
                          &venc_channel_count);
    for (int i = 0; i < venc_channel_count; ++i) {
        DestroyVencDirect(venc_channels[i]);
    }

    if (config.vpss_group >= 0) {
        const VPSS_GRP vpss_group = static_cast<VPSS_GRP>(config.vpss_group);
        (void)HI_MPI_VPSS_DisableBackupFrame(vpss_group);
        for (int i = 0; i < VPSS_MAX_PHY_CHN_NUM; ++i) {
            (void)HI_MPI_VPSS_DisableChn(vpss_group,
                                         static_cast<VPSS_CHN>(i));
        }
        (void)HI_MPI_VPSS_StopGrp(vpss_group);
        (void)HI_MPI_VPSS_DestroyGrp(vpss_group);
    }

    UnbindViVpssDirect(config);
    if (config.video_pipe >= 0) {
        const VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
        (void)HI_MPI_ISP_Exit(vi_pipe);
        if (config.vi_channel >= 0) {
            (void)HI_MPI_VI_DisableChn(
                vi_pipe, static_cast<VI_CHN>(config.vi_channel));
        }
        (void)HI_MPI_VI_StopPipe(vi_pipe);
        (void)HI_MPI_VI_DestroyPipe(vi_pipe);
    }
    if (config.sensor_id >= 0) {
        (void)HI_MPI_VI_DisableDev(static_cast<VI_DEV>(config.sensor_id));
    }
    usleep(kMppExitRetryDelayUs);
}

bool VbPoolCompatible(const VB_POOL_CONFIG_S& current,
                      const VB_POOL_CONFIG_S& expected) {
    if (expected.u32BlkCnt == 0) {
        return true;
    }
    return current.u64BlkSize == expected.u64BlkSize &&
           current.u32BlkCnt >= expected.u32BlkCnt &&
           current.enRemapMode == expected.enRemapMode &&
           std::strncmp(current.acMmzName, expected.acMmzName,
                        sizeof(current.acMmzName)) == 0;
}

bool VbConfigCompatible(const VB_CONFIG_S& current,
                        const VB_CONFIG_S& expected) {
    if (current.u32MaxPoolCnt < expected.u32MaxPoolCnt) {
        return false;
    }
    for (HI_U32 i = 0; i < expected.u32MaxPoolCnt && i < VB_MAX_COMM_POOLS;
         ++i) {
        if (!VbPoolCompatible(current.astCommPool[i],
                              expected.astCommPool[i])) {
            return false;
        }
    }
    return true;
}

void LogVbConfigSummary(const char* prefix, const VB_CONFIG_S& config) {
    if (prefix == nullptr) {
        return;
    }
    INFRA_LOG_WARN(
        "hisi_vendor",
        "%s max_pool=%u pool0=%llu/%u pool1=%llu/%u pool2=%llu/%u "
        "pool3=%llu/%u",
        prefix, config.u32MaxPoolCnt,
        static_cast<unsigned long long>(config.astCommPool[0].u64BlkSize),
        config.astCommPool[0].u32BlkCnt,
        static_cast<unsigned long long>(config.astCommPool[1].u64BlkSize),
        config.astCommPool[1].u32BlkCnt,
        static_cast<unsigned long long>(config.astCommPool[2].u64BlkSize),
        config.astCommPool[2].u32BlkCnt,
        static_cast<unsigned long long>(config.astCommPool[3].u64BlkSize),
        config.astCommPool[3].u32BlkCnt);
}

bool TryReuseExistingVideoBuffer(const VB_CONFIG_S& expected) {
    VB_CONFIG_S current{};
    HI_S32 status = HI_MPI_VB_GetConfig(&current);
    if (status != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VB_GetConfig failed while reusing VB: 0x%08x",
                        status);
        return false;
    }

    if (!VbConfigCompatible(current, expected)) {
        LogVbConfigSummary("existing incompatible VB config", current);
        LogVbConfigSummary("expected VB config", expected);
        return false;
    }

    status = HI_MPI_VB_Init();
    if (status != HI_SUCCESS && status != HI_ERR_VB_BUSY &&
        status != HI_ERR_VB_NOT_PERM) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VB_Init failed while reusing VB: 0x%08x",
                        status);
        return false;
    }

    status = HI_MPI_SYS_Init();
    if (status != HI_SUCCESS && status != HI_ERR_SYS_NOT_PERM) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_SYS_Init failed while reusing VB: 0x%08x",
                        status);
        return false;
    }

    INFRA_LOG_WARN("hisi_vendor",
                   "reuse existing compatible HISI VB configuration");
    return true;
}

bool InitConfiguredVideoBuffer() {
    HI_S32 status = HI_MPI_VB_Init();
    if (status != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor", "HI_MPI_VB_Init failed: 0x%08x",
                        status);
        (void)ExitMppSystem(true, kMppExitRetryCount);
        return false;
    }

    status = HI_MPI_SYS_Init();
    if (status != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor", "HI_MPI_SYS_Init failed: 0x%08x",
                        status);
        (void)ExitMppSystem(true, kMppExitRetryCount);
        return false;
    }

    return true;
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

    // Match the HiSilicon sample sequence and clear stale global MPP state
    // before VB_SetConfig. A killed previous process can leave VB initialized.
    (void)ExitMppSystem(false, 1);

    HI_S32 status = HI_MPI_VB_SetConfig(&vb_conf);
    if (status == HI_ERR_VB_BUSY) {
        ForceCleanupPipelineResources(config);
        (void)ExitMppSystem(true, kMppExitRetryCount);
        status = HI_MPI_VB_SetConfig(&vb_conf);
    }
    if (status == HI_ERR_VB_BUSY && TryReuseExistingVideoBuffer(vb_conf)) {
        return true;
    }
    if (status != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VB_SetConfig(&vb_conf) failed: 0x%08x",
                        status);
        return false;
    }

    return InitConfiguredVideoBuffer();
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
        (void)ExitMppSystem(true, kMppExitRetryCount);
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
    if (!ExitMppSystem(true, kMppExitRetryCount)) {
        ForceCleanupPipelineResources(config);
        (void)ExitMppSystem(true, kMppExitRetryCount);
    }
#endif

    impl_->system_initialized_ = false;
    impl_->has_active_config_ = false;
}

}  // namespace hisisdk
}  // namespace live_stream
