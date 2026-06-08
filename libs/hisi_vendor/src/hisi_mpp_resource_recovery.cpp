#include "hisi_mpp_resource_recovery.h"

#include <unistd.h>

#include "hisi_mpp_utils.h"
#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace hisisdk {
namespace mpp_resource_recovery {
namespace {

constexpr useconds_t kMppExitRetryDelayUs = 100 * 1000;
constexpr int kMaxCleanupVencChannels = 4;

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
    Warn("hisi_vendor", "%s during cleanup returned 0x%08x", step, status);
}

HI_S32 ExitMppSystemOnce(bool log_errors) {
    HI_S32 status = HI_MPI_SYS_Exit();
    LogCleanupStatus("HI_MPI_SYS_Exit", status, log_errors);

    status = HI_MPI_VB_ExitModCommPool(VB_UID_VDEC);
    LogCleanupStatus("HI_MPI_VB_ExitModCommPool(VB_UID_VDEC)", status,
                     log_errors);

    status = HI_MPI_VB_Exit();
    if (status != HI_ERR_VB_BUSY) {
        LogCleanupStatus("HI_MPI_VB_Exit", status, log_errors);
    }
    return status;
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

}  // namespace

bool ExitMppSystem(bool log_errors, int retry_count,
                   MppExitBusyLog busy_log) {
    HI_S32 status = HI_SUCCESS;
    for (int attempt = 0; attempt <= retry_count; ++attempt) {
        status = ExitMppSystemOnce(log_errors);
        if (IsVbExitDone(status)) {
            return true;
        }
        if (status != HI_ERR_VB_BUSY || attempt == retry_count) {
            break;
        }
        usleep(kMppExitRetryDelayUs);
    }
    if (log_errors && status == HI_ERR_VB_BUSY) {
        if (busy_log == MppExitBusyLog::kWarn) {
            Warn("hisi_vendor",
                 "HI_MPI_VB_Exit still busy after %d retries", retry_count);
        } else if (busy_log == MppExitBusyLog::kInfo) {
            Info("hisi_vendor",
                 "HI_MPI_VB_Exit still busy after %d retries; "
                 "continuing recovery",
                 retry_count);
        }
    }
    return false;
}

void ForceCleanupPipelineResources(const MediaPipelineConfig& config,
                                   bool warn) {
    if (warn) {
        Warn("hisi_vendor",
             "stale HISI MPP resources detected, force cleanup known channels");
    } else {
        Info("hisi_vendor",
             "stale HISI MPP resources detected, force cleanup known channels");
    }

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

}  // namespace mpp_resource_recovery
}  // namespace hisisdk
}  // namespace live_stream
