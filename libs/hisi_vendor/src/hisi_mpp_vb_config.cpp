#include "hisi_mpp_vb_config.h"

#include "hisi_mpp_sensor.h"
#include "hisi_mpp_sdk.h"
#include "hisi_mpp_system_cleanup.h"

#include "infra/log.h"

namespace live_stream {
namespace hisisdk {
namespace mpp_vb_config {
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

VideoSize SensorInputSize() {
    const internal::SensorProfile& profile = internal::SelectedSensorProfile();
    return VideoSize{profile.input_width, profile.input_height};
}

VB_CONFIG_S BuildFrameBufferConfig(const MediaPipelineConfig& config) {
    VB_CONFIG_S vb_conf{};
    const VideoSize sensor_input_size = SensorInputSize();

    vb_conf.astCommPool[0].u64BlkSize =
        Yuv420BlockSize(sensor_input_size);
    vb_conf.astCommPool[0].u32BlkCnt = config.vb_blocks;
    vb_conf.astCommPool[0].enRemapMode = VB_REMAP_MODE_CACHED;

    vb_conf.astCommPool[1].u64BlkSize =
        Yuv420BlockSize(config.main_stream.size);
    vb_conf.astCommPool[1].u32BlkCnt = 4;
    vb_conf.astCommPool[1].enRemapMode = VB_REMAP_MODE_CACHED;

    if (config.sub_stream.enabled) {
        vb_conf.astCommPool[2].u64BlkSize =
            Yuv420BlockSize(config.sub_stream.size);
        vb_conf.astCommPool[2].u32BlkCnt = 4;
        vb_conf.astCommPool[2].enRemapMode = VB_REMAP_MODE_CACHED;
    }

    vb_conf.u32MaxPoolCnt = 4;
    return vb_conf;
}

void MarkCleanupFailure(bool* cleanup_failed) {
    if (cleanup_failed != nullptr) {
        *cleanup_failed = true;
    }
}

bool CleanupConfiguredFrameBuffer(bool* cleanup_failed) {
    if (mpp_system_cleanup::ExitMppSystem(
            true, mpp_system_cleanup::kMppExitRetryLimit,
            mpp_system_cleanup::MppExitBusyLog::kWarn)) {
        return true;
    }
    MarkCleanupFailure(cleanup_failed);
    return false;
}

bool InitConfiguredFrameBuffer(bool* cleanup_failed) {
    HI_S32 status = HI_MPI_VB_Init();
    if (status != HI_SUCCESS) {
        Error("hisi_vendor", "HI_MPI_VB_Init failed: 0x%08x", status);
        (void)CleanupConfiguredFrameBuffer(cleanup_failed);
        return false;
    }

    status = HI_MPI_SYS_Init();
    if (status != HI_SUCCESS) {
        Error("hisi_vendor", "HI_MPI_SYS_Init failed: 0x%08x", status);
        (void)CleanupConfiguredFrameBuffer(cleanup_failed);
        return false;
    }

    return true;
}

}  // namespace

bool ConfigureFrameBuffer(const MediaPipelineConfig& config,
                          bool* cleanup_failed) {
    if (cleanup_failed != nullptr) {
        *cleanup_failed = false;
    }
    const VB_CONFIG_S vb_conf = BuildFrameBufferConfig(config);

    (void)mpp_system_cleanup::ExitMppSystem(
        false, 1, mpp_system_cleanup::MppExitBusyLog::kSilent);

    HI_S32 status = HI_MPI_VB_SetConfig(&vb_conf);
    if (status == HI_ERR_VB_BUSY) {
        Info("hisi_vendor",
             "HISI VB is busy before configuration, trying recovery");
        mpp_system_cleanup::ForceCleanupPipelineChannels(config, false);
        if (!mpp_system_cleanup::ExitMppSystem(
                true, mpp_system_cleanup::kMppExitRetryLimit,
                mpp_system_cleanup::MppExitBusyLog::kInfo)) {
            Error("hisi_vendor",
                  "HISI MPP cleanup failed before VB_SetConfig");
            MarkCleanupFailure(cleanup_failed);
            return false;
        }
        status = HI_MPI_VB_SetConfig(&vb_conf);
    }
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VB_SetConfig(&vb_conf) failed: 0x%08x", status);
        if (status == HI_ERR_VB_BUSY) {
            MarkCleanupFailure(cleanup_failed);
        }
        return false;
    }

    return InitConfiguredFrameBuffer(cleanup_failed);
}

}  // namespace mpp_vb_config
}  // namespace hisisdk
}  // namespace live_stream
