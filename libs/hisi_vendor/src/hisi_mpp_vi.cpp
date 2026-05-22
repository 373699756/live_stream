#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_sensor.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <fcntl.h>
#include <cstdint>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>

namespace live_stream {
namespace hisisdk {

namespace {

constexpr const char* kMipiDeviceNode = "/dev/hi_mipi";

using internal::SelectedSensorProfile;
using internal::SensorProfile;

bool CheckMpiCall(const char* expression, HI_S32 status) {
    if (status == HI_SUCCESS) {
        return true;
    }
    INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", expression, status);
    return false;
}

bool CheckSysCall(const char* expression, int status) {
    if (status == 0) {
        return true;
    }
    INFRA_LOG_ERROR("hisi_vendor", "%s failed: %d", expression, status);
    return false;
}

void FillAlgLib(const char* name, VI_PIPE vi_pipe, ALG_LIB_S* lib) {
    if (lib == nullptr) {
        return;
    }
    lib->s32Id = vi_pipe;
    std::strncpy(lib->acLibName, name, sizeof(lib->acLibName) - 1);
    lib->acLibName[sizeof(lib->acLibName) - 1] = '\0';
}

void UnregisterSensorCallback(const SensorProfile& profile, VI_PIPE vi_pipe,
                              ALG_LIB_S* ae_lib, ALG_LIB_S* awb_lib) {
    if (profile.sns_obj != nullptr &&
        profile.sns_obj->pfnUnRegisterCallback != nullptr) {
        (void)profile.sns_obj->pfnUnRegisterCallback(vi_pipe, ae_lib, awb_lib);
    }
}

VI_PIPE_ATTR_S MakePipeAttr(const SensorProfile& profile) {
    VI_PIPE_ATTR_S pipe_attr{};
    pipe_attr.enPipeBypassMode = VI_PIPE_BYPASS_NONE;
    pipe_attr.bYuvSkip = HI_FALSE;
    pipe_attr.bIspBypass = HI_FALSE;
    pipe_attr.u32MaxW = profile.input_width;
    pipe_attr.u32MaxH = profile.input_height;
    pipe_attr.enPixFmt = profile.pipe_pixel_format;
    pipe_attr.enCompressMode = profile.pipe_compress_mode;
    pipe_attr.enBitWidth = profile.pipe_bit_width;
    pipe_attr.bNrEn = profile.pipe_nr_enabled;
    pipe_attr.stNrAttr.enPixFmt = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    pipe_attr.stNrAttr.enBitWidth = DATA_BITWIDTH_8;
    pipe_attr.stNrAttr.enNrRefSource = VI_NR_REF_FROM_RFR;
    pipe_attr.stNrAttr.enCompressMode = COMPRESS_MODE_NONE;
    pipe_attr.bSharpenEn = HI_FALSE;
    pipe_attr.stFrameRate.s32SrcFrameRate = -1;
    pipe_attr.stFrameRate.s32DstFrameRate = -1;
    pipe_attr.bDiscardProPic = HI_FALSE;
    return pipe_attr;
}

VI_CHN_ATTR_S MakeViChannelAttr(const MediaPipelineConfig& config) {
    VI_CHN_ATTR_S chn_attr{};
    chn_attr.stSize.u32Width = config.main_stream.size.width;
    chn_attr.stSize.u32Height = config.main_stream.size.height;
    chn_attr.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    chn_attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
    chn_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
    chn_attr.enCompressMode = COMPRESS_MODE_NONE;
    chn_attr.bMirror = HI_FALSE;
    chn_attr.bFlip = HI_FALSE;
    chn_attr.u32Depth = 0;
    chn_attr.stFrameRate.s32SrcFrameRate = -1;
    chn_attr.stFrameRate.s32DstFrameRate = -1;
    return chn_attr;
}

VI_DEV_ATTR_S MakeViDevAttr(const SensorProfile& profile) {
    VI_DEV_ATTR_S dev_attr{};
    dev_attr.enIntfMode = VI_MODE_MIPI;
    dev_attr.enWorkMode = VI_WORK_MODE_1Multiplex;
    dev_attr.au32ComponentMask[0] = profile.vi_component_mask;
    dev_attr.au32ComponentMask[1] = 0;
    dev_attr.enScanMode = VI_SCAN_PROGRESSIVE;
    dev_attr.as32AdChnId[0] = -1;
    dev_attr.as32AdChnId[1] = -1;
    dev_attr.as32AdChnId[2] = -1;
    dev_attr.as32AdChnId[3] = -1;
    dev_attr.enDataSeq = VI_DATA_SEQ_YUYV;
    dev_attr.stSynCfg.enVsync = VI_VSYNC_PULSE;
    dev_attr.stSynCfg.enVsyncNeg = VI_VSYNC_NEG_LOW;
    dev_attr.stSynCfg.enHsync = VI_HSYNC_VALID_SINGNAL;
    dev_attr.stSynCfg.enHsyncNeg = VI_HSYNC_NEG_HIGH;
    dev_attr.stSynCfg.enVsyncValid = VI_VSYNC_VALID_SINGAL;
    dev_attr.stSynCfg.enVsyncValidNeg = VI_VSYNC_VALID_NEG_HIGH;
    dev_attr.stSynCfg.stTimingBlank.u32HsyncHfb = 0;
    dev_attr.stSynCfg.stTimingBlank.u32HsyncAct = 1280;
    dev_attr.stSynCfg.stTimingBlank.u32HsyncHbb = 0;
    dev_attr.stSynCfg.stTimingBlank.u32VsyncVfb = 0;
    dev_attr.stSynCfg.stTimingBlank.u32VsyncVact = 720;
    dev_attr.stSynCfg.stTimingBlank.u32VsyncVbb = 0;
    dev_attr.stSynCfg.stTimingBlank.u32VsyncVbfb = 0;
    dev_attr.stSynCfg.stTimingBlank.u32VsyncVbact = 0;
    dev_attr.stSynCfg.stTimingBlank.u32VsyncVbbb = 0;
    dev_attr.enInputDataType = VI_DATA_TYPE_RGB;
    dev_attr.bDataReverse = HI_FALSE;
    dev_attr.stSize.u32Width = profile.input_width;
    dev_attr.stSize.u32Height = profile.input_height;
    dev_attr.stBasAttr.stSacleAttr.stBasSize.u32Width =
        profile.input_width;
    dev_attr.stBasAttr.stSacleAttr.stBasSize.u32Height =
        profile.input_height;
    dev_attr.stBasAttr.stRephaseAttr.enHRephaseMode = VI_REPHASE_MODE_NONE;
    dev_attr.stBasAttr.stRephaseAttr.enVRephaseMode = VI_REPHASE_MODE_NONE;
    dev_attr.stWDRAttr.enWDRMode = profile.wdr_mode;
    dev_attr.stWDRAttr.u32CacheLine = profile.input_height;
    dev_attr.enDataRate = DATA_RATE_X1;
    return dev_attr;
}

combo_dev_attr_t MakeMipiAttr(const SensorProfile& profile) {
    combo_dev_attr_t attr{};
    attr.devno = 0;
    attr.input_mode = INPUT_MODE_MIPI;
    attr.data_rate = MIPI_DATA_RATE_X1;
    attr.img_rect.x = profile.mipi_x;
    attr.img_rect.y = profile.mipi_y;
    attr.img_rect.width = profile.input_width;
    attr.img_rect.height = profile.input_height;
    attr.mipi_attr.input_data_type = profile.mipi_data_type;
    attr.mipi_attr.wdr_mode = profile.mipi_wdr_mode;
    std::memcpy(attr.mipi_attr.lane_id, profile.lane_id,
                sizeof(attr.mipi_attr.lane_id));
    return attr;
}

ISP_PUB_ATTR_S MakeIspPubAttr(const SensorProfile& profile) {
    ISP_PUB_ATTR_S attr{};
    attr.stWndRect.s32X = 0;
    attr.stWndRect.s32Y = 0;
    attr.stWndRect.u32Width = profile.input_width;
    attr.stWndRect.u32Height = profile.input_height;
    attr.stSnsSize.u32Width = profile.sensor_width;
    attr.stSnsSize.u32Height = profile.sensor_height;
    attr.f32FrameRate = profile.frame_rate;
    attr.enBayer = profile.bayer;
    attr.enWDRMode = profile.wdr_mode;
    attr.u8SnsMode = profile.sns_mode;
    return attr;
}

bool MipiIoctl(const char* expression, unsigned long request, void* arg) {
    const int fd = open(kMipiDeviceNode, O_RDWR);
    if (fd < 0) {
        INFRA_LOG_ERROR("hisi_vendor", "open %s failed", kMipiDeviceNode);
        return false;
    }
    const int status = ioctl(fd, request, arg);
    close(fd);
    return CheckSysCall(expression, status);
}

void StopMipi();

bool StartMipi(const SensorProfile& profile) {
    lane_divide_mode_t lane_mode = LANE_DIVIDE_MODE_0;
    if (!MipiIoctl("HI_MIPI_SET_HS_MODE", HI_MIPI_SET_HS_MODE, &lane_mode)) {
        return false;
    }

    combo_dev_t mipi_dev = 0;
    if (!MipiIoctl("HI_MIPI_ENABLE_MIPI_CLOCK", HI_MIPI_ENABLE_MIPI_CLOCK,
                   &mipi_dev)) {
        return false;
    }
    if (!MipiIoctl("HI_MIPI_RESET_MIPI", HI_MIPI_RESET_MIPI, &mipi_dev)) {
        StopMipi();
        return false;
    }

    for (sns_clk_source_t sns_clk = 0; sns_clk < SNS_MAX_CLK_SOURCE_NUM;
         ++sns_clk) {
        if (!MipiIoctl("HI_MIPI_ENABLE_SENSOR_CLOCK",
                       HI_MIPI_ENABLE_SENSOR_CLOCK, &sns_clk)) {
            StopMipi();
            return false;
        }
    }
    for (sns_rst_source_t sns_rst = 0; sns_rst < SNS_MAX_RST_SOURCE_NUM;
         ++sns_rst) {
        if (!MipiIoctl("HI_MIPI_RESET_SENSOR", HI_MIPI_RESET_SENSOR, &sns_rst)) {
            StopMipi();
            return false;
        }
    }

    combo_dev_attr_t combo_attr = MakeMipiAttr(profile);
    if (!MipiIoctl("HI_MIPI_SET_DEV_ATTR", HI_MIPI_SET_DEV_ATTR,
                   &combo_attr)) {
        StopMipi();
        return false;
    }
    if (!MipiIoctl("HI_MIPI_UNRESET_MIPI", HI_MIPI_UNRESET_MIPI, &mipi_dev)) {
        StopMipi();
        return false;
    }
    for (sns_rst_source_t sns_rst = 0; sns_rst < SNS_MAX_RST_SOURCE_NUM;
         ++sns_rst) {
        if (!MipiIoctl("HI_MIPI_UNRESET_SENSOR", HI_MIPI_UNRESET_SENSOR,
                       &sns_rst)) {
            StopMipi();
            return false;
        }
    }
    return true;
}

void StopMipi() {
    for (sns_rst_source_t sns_rst = 0; sns_rst < SNS_MAX_RST_SOURCE_NUM;
         ++sns_rst) {
        (void)MipiIoctl("HI_MIPI_RESET_SENSOR", HI_MIPI_RESET_SENSOR, &sns_rst);
    }
    for (sns_clk_source_t sns_clk = 0; sns_clk < SNS_MAX_CLK_SOURCE_NUM;
         ++sns_clk) {
        (void)MipiIoctl("HI_MIPI_DISABLE_SENSOR_CLOCK",
                        HI_MIPI_DISABLE_SENSOR_CLOCK, &sns_clk);
    }
    combo_dev_t mipi_dev = 0;
    (void)MipiIoctl("HI_MIPI_RESET_MIPI", HI_MIPI_RESET_MIPI, &mipi_dev);
    (void)MipiIoctl("HI_MIPI_DISABLE_MIPI_CLOCK", HI_MIPI_DISABLE_MIPI_CLOCK,
                    &mipi_dev);
}

void* IspThread(void* arg) {
    const VI_PIPE vi_pipe = static_cast<VI_PIPE>(
        reinterpret_cast<intptr_t>(arg));
    const HI_S32 status = HI_MPI_ISP_Run(vi_pipe);
    if (status != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor", "HI_MPI_ISP_Run pipe %d failed: 0x%08x",
                        vi_pipe, status);
    }
    return nullptr;
}

bool StartIsp(VI_PIPE vi_pipe, const SensorProfile& profile,
              pthread_t* isp_thread) {
    if (isp_thread == nullptr) {
        return false;
    }

    ALG_LIB_S ae_lib{};
    ALG_LIB_S awb_lib{};
    FillAlgLib(HI_AE_LIB_NAME, vi_pipe, &ae_lib);
    FillAlgLib(HI_AWB_LIB_NAME, vi_pipe, &awb_lib);

    if (profile.sns_obj == nullptr ||
        profile.sns_obj->pfnRegisterCallback == nullptr ||
        profile.sns_obj->pfnSetBusInfo == nullptr) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "%s sensor callbacks are unavailable", profile.name);
        return false;
    }

    if (!CheckMpiCall("sensor.pfnRegisterCallback",
                      profile.sns_obj->pfnRegisterCallback(vi_pipe, &ae_lib,
                                                           &awb_lib))) {
        return false;
    }

    ISP_SNS_COMMBUS_U bus_info{};
    bus_info.s8I2cDev = profile.i2c_device;
    if (!CheckMpiCall("sensor.pfnSetBusInfo",
                      profile.sns_obj->pfnSetBusInfo(vi_pipe, bus_info))) {
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }

    if (!CheckMpiCall("HI_MPI_AE_Register",
                      HI_MPI_AE_Register(vi_pipe, &ae_lib))) {
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }
    if (!CheckMpiCall("HI_MPI_AWB_Register",
                      HI_MPI_AWB_Register(vi_pipe, &awb_lib))) {
        (void)HI_MPI_AE_UnRegister(vi_pipe, &ae_lib);
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }

    if (!CheckMpiCall("HI_MPI_ISP_MemInit", HI_MPI_ISP_MemInit(vi_pipe))) {
        (void)HI_MPI_AWB_UnRegister(vi_pipe, &awb_lib);
        (void)HI_MPI_AE_UnRegister(vi_pipe, &ae_lib);
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }

    ISP_PUB_ATTR_S pub_attr = MakeIspPubAttr(profile);
    if (!CheckMpiCall("HI_MPI_ISP_SetPubAttr",
                      HI_MPI_ISP_SetPubAttr(vi_pipe, &pub_attr))) {
        (void)HI_MPI_AWB_UnRegister(vi_pipe, &awb_lib);
        (void)HI_MPI_AE_UnRegister(vi_pipe, &ae_lib);
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }

    if (!CheckMpiCall("HI_MPI_ISP_Init", HI_MPI_ISP_Init(vi_pipe))) {
        (void)HI_MPI_AWB_UnRegister(vi_pipe, &awb_lib);
        (void)HI_MPI_AE_UnRegister(vi_pipe, &ae_lib);
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }

    const int thread_status = pthread_create(
        isp_thread, nullptr, IspThread,
        reinterpret_cast<void*>(static_cast<intptr_t>(vi_pipe)));
    if (!CheckSysCall("pthread_create(ISP)", thread_status)) {
        (void)HI_MPI_ISP_Exit(vi_pipe);
        (void)HI_MPI_AWB_UnRegister(vi_pipe, &awb_lib);
        (void)HI_MPI_AE_UnRegister(vi_pipe, &ae_lib);
        UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
        return false;
    }

    return true;
}

void StopIsp(VI_PIPE vi_pipe, const SensorProfile& profile,
             pthread_t isp_thread) {
    (void)HI_MPI_ISP_Exit(vi_pipe);
    if (isp_thread != 0) {
        (void)pthread_join(isp_thread, nullptr);
    }

    ALG_LIB_S ae_lib{};
    ALG_LIB_S awb_lib{};
    FillAlgLib(HI_AE_LIB_NAME, vi_pipe, &ae_lib);
    FillAlgLib(HI_AWB_LIB_NAME, vi_pipe, &awb_lib);
    (void)HI_MPI_AWB_UnRegister(vi_pipe, &awb_lib);
    (void)HI_MPI_AE_UnRegister(vi_pipe, &ae_lib);
    UnregisterSensorCallback(profile, vi_pipe, &ae_lib, &awb_lib);
}

bool BindDevPipe(VI_DEV vi_dev, VI_PIPE vi_pipe) {
    VI_DEV_BIND_PIPE_S bind_pipe{};
    bind_pipe.u32Num = 1;
    bind_pipe.PipeId[0] = vi_pipe;
    return CheckMpiCall("HI_MPI_VI_SetDevBindPipe",
                        HI_MPI_VI_SetDevBindPipe(vi_dev, &bind_pipe));
}

void StopPipe(VI_PIPE vi_pipe) {
    (void)HI_MPI_VI_StopPipe(vi_pipe);
    (void)HI_MPI_VI_DestroyPipe(vi_pipe);
}

void CleanupStartedVi(VI_DEV vi_dev, VI_PIPE vi_pipe, VI_CHN vi_chn,
                      bool chn_enabled, bool pipe_created,
                      bool dev_enabled, bool mipi_started) {
    if (chn_enabled) {
        (void)HI_MPI_VI_DisableChn(vi_pipe, vi_chn);
    }
    if (pipe_created) {
        StopPipe(vi_pipe);
    }
    if (dev_enabled) {
        (void)HI_MPI_VI_DisableDev(vi_dev);
    }
    if (mipi_started) {
        StopMipi();
    }
}

}  // namespace

bool MppHisiSdk::StartVi(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (impl_->vi_started_) return true;
    const SensorProfile& sensor_profile = SelectedSensorProfile();

    bool dev_enabled = false;
    bool pipe_created = false;
    bool chn_enabled = false;

    if (!impl_->mipi_started_) {
        if (!StartMipi(sensor_profile)) {
            return false;
        }
        impl_->mipi_started_ = true;
    }

    // ─── VI DEV attribute ─────────────────────────────────────
    VI_DEV_ATTR_S dev_attr = MakeViDevAttr(sensor_profile);
    VI_DEV vi_dev = static_cast<VI_DEV>(config.sensor_id);
    if (!CheckMpiCall("HI_MPI_VI_SetDevAttr",
                      HI_MPI_VI_SetDevAttr(vi_dev, &dev_attr))) {
        CleanupStartedVi(vi_dev, static_cast<VI_PIPE>(config.video_pipe),
                         static_cast<VI_CHN>(config.vi_channel), chn_enabled,
                         pipe_created, dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }
    if (!CheckMpiCall("HI_MPI_VI_EnableDev",
                      HI_MPI_VI_EnableDev(vi_dev))) {
        CleanupStartedVi(vi_dev, static_cast<VI_PIPE>(config.video_pipe),
                         static_cast<VI_CHN>(config.vi_channel), chn_enabled,
                         pipe_created, dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }
    dev_enabled = true;

    VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    VI_CHN vi_chn = static_cast<VI_CHN>(config.vi_channel);
    if (!BindDevPipe(vi_dev, vi_pipe)) {
        CleanupStartedVi(vi_dev, vi_pipe, vi_chn, chn_enabled, pipe_created,
                         dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }

    VI_PIPE_ATTR_S pipe_attr = MakePipeAttr(sensor_profile);
    if (!CheckMpiCall("HI_MPI_VI_CreatePipe",
                      HI_MPI_VI_CreatePipe(vi_pipe, &pipe_attr))) {
        CleanupStartedVi(vi_dev, vi_pipe, vi_chn, chn_enabled, pipe_created,
                         dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }
    pipe_created = true;
    if (!CheckMpiCall("HI_MPI_VI_StartPipe",
                      HI_MPI_VI_StartPipe(vi_pipe))) {
        CleanupStartedVi(vi_dev, vi_pipe, vi_chn, chn_enabled, pipe_created,
                         dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }

    // ─── VI CHN attribute ─────────────────────────────────────
    VI_CHN_ATTR_S chn_attr = MakeViChannelAttr(config);

    if (!CheckMpiCall("HI_MPI_VI_SetChnAttr",
                      HI_MPI_VI_SetChnAttr(vi_pipe, vi_chn, &chn_attr))) {
        CleanupStartedVi(vi_dev, vi_pipe, vi_chn, chn_enabled, pipe_created,
                         dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }
    if (!CheckMpiCall("HI_MPI_VI_EnableChn",
                      HI_MPI_VI_EnableChn(vi_pipe, vi_chn))) {
        CleanupStartedVi(vi_dev, vi_pipe, vi_chn, chn_enabled, pipe_created,
                         dev_enabled, impl_->mipi_started_);
        impl_->mipi_started_ = false;
        return false;
    }
    chn_enabled = true;

    if (!impl_->isp_started_) {
        if (!StartIsp(vi_pipe, sensor_profile, &impl_->isp_thread_)) {
            CleanupStartedVi(vi_dev, vi_pipe, vi_chn, chn_enabled, pipe_created,
                             dev_enabled, impl_->mipi_started_);
            impl_->mipi_started_ = false;
            return false;
        }
        impl_->isp_started_ = true;
    }

    impl_->vi_started_ = true;
    return true;
}

void MppHisiSdk::StopVi(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (!impl_->vi_started_) return;
    const SensorProfile& sensor_profile = SelectedSensorProfile();

    VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    VI_CHN vi_chn = static_cast<VI_CHN>(config.vi_channel);
    if (impl_->isp_started_) {
        StopIsp(vi_pipe, sensor_profile, impl_->isp_thread_);
        impl_->isp_thread_ = 0;
        impl_->isp_started_ = false;
    }
    HI_MPI_VI_DisableChn(vi_pipe, vi_chn);
    StopPipe(vi_pipe);
    HI_MPI_VI_DisableDev(static_cast<VI_DEV>(config.sensor_id));
    if (impl_->mipi_started_) {
        StopMipi();
        impl_->mipi_started_ = false;
    }

    impl_->vi_started_ = false;
}

}  // namespace hisisdk
}  // namespace live_stream
