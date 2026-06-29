#include "hisi_mpp_image_controls.h"

#include "hisi_mpp_sdk.h"
#include "infra/log.h"
#include "json_reader.h"
#include "mpp_hisi_sdk_impl.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace hisisdk {

namespace {

constexpr int32_t kLdcRatioMin = 0;
constexpr int32_t kLdcRatioMax = 100;
constexpr int32_t kLdcCenterOffsetMin = -511;
constexpr int32_t kLdcCenterOffsetMax = 511;
constexpr int32_t kLdcDistortionMin = -300;
constexpr int32_t kLdcDistortionMax = 500;
constexpr int32_t kDisCropRatioMin = 50;
constexpr int32_t kDisCropRatioMax = 98;
constexpr int32_t kDisBufferSizeMin = 5;
constexpr int32_t kDisBufferSizeMax = 10;
constexpr int32_t kDisFrameRateMin = 1;
constexpr int32_t kDisFrameRateMax = 60;
constexpr int32_t kDisMovingSubjectLevelMin = 0;
constexpr int32_t kDisMovingSubjectLevelMax = 6;
constexpr int32_t kDisRollingShutterCoefMin = 0;
constexpr int32_t kDisRollingShutterCoefMax = 1000;
constexpr int32_t kDisDriftLimitMin = 0;
constexpr int32_t kDisDriftLimitMax = 1000;

bool FindSection(const Json& image_config, const char* section_name,
                 const Json** section) {
    if (section == nullptr || section_name == nullptr ||
        !image_config.contains(section_name) ||
        !image_config.at(section_name).is_object()) {
        return false;
    }
    *section = &image_config.at(section_name);
    return true;
}

bool ApplyVpssChannelLdc(VPSS_GRP vpss_group, VPSS_CHN vpss_channel,
                         const Json& lens_correction) {
    bool enabled = false;
    if (!json_reader::ReadField(lens_correction, "enabled", &enabled)) {
        return false;
    }

    VPSS_LDC_ATTR_S attr{};
    attr.bEnable = enabled ? HI_TRUE : HI_FALSE;
    attr.stAttr.bAspect = HI_TRUE;
    attr.stAttr.s32XRatio = kLdcRatioMax;
    attr.stAttr.s32YRatio = kLdcRatioMax;
    attr.stAttr.s32XYRatio = kLdcRatioMax;

    bool aspect = true;
    if (json_reader::ReadField(lens_correction, "aspect", &aspect)) {
        attr.stAttr.bAspect = aspect ? HI_TRUE : HI_FALSE;
    }
    int32_t value = 0;
    if (json_reader::ReadField(lens_correction, "x_ratio", &value,
                               kLdcRatioMin, kLdcRatioMax)) {
        attr.stAttr.s32XRatio = value;
    }
    if (json_reader::ReadField(lens_correction, "y_ratio", &value,
                               kLdcRatioMin, kLdcRatioMax)) {
        attr.stAttr.s32YRatio = value;
    }
    if (json_reader::ReadField(lens_correction, "xy_ratio", &value,
                               kLdcRatioMin, kLdcRatioMax)) {
        attr.stAttr.s32XYRatio = value;
    }
    if (json_reader::ReadField(lens_correction, "center_x_offset", &value,
                               kLdcCenterOffsetMin, kLdcCenterOffsetMax)) {
        attr.stAttr.s32CenterXOffset = value;
    }
    if (json_reader::ReadField(lens_correction, "center_y_offset", &value,
                               kLdcCenterOffsetMin, kLdcCenterOffsetMax)) {
        attr.stAttr.s32CenterYOffset = value;
    }
    if (json_reader::ReadField(lens_correction, "distortion_ratio", &value,
                               kLdcDistortionMin, kLdcDistortionMax)) {
        attr.stAttr.s32DistortionRatio = value;
    }

    const HI_S32 status =
        HI_MPI_VPSS_SetChnLDCAttr(vpss_group, vpss_channel, &attr);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VPSS_SetChnLDCAttr grp=%d chn=%d failed: 0x%08x",
              vpss_group, vpss_channel, status);
        return false;
    }
    return true;
}

bool IsLdcStreamSizeSupported(const VideoStreamConfig& stream_config) {
    return stream_config.size.width >= LDC_MIN_IMAGE_WIDTH &&
           stream_config.size.height >= LDC_MIN_IMAGE_HEIGHT;
}

bool ApplyLensCorrection(const MediaPipelineConfig& config,
                         const Json& image_config) {
    const Json* lens_correction = nullptr;
    Json disabled_lens_correction = Json::object();
    if (!FindSection(image_config, "lens_correction", &lens_correction)) {
        disabled_lens_correction["enabled"] = false;
        lens_correction = &disabled_lens_correction;
    }
    bool enabled = false;
    if (!json_reader::ReadField(*lens_correction, "enabled", &enabled)) {
        return false;
    }
    if (enabled && !IsLdcStreamSizeSupported(config.main_stream)) {
        Error("hisi_vendor", "VPSS LDC main stream size unsupported: %ux%u",
              config.main_stream.size.width, config.main_stream.size.height);
        return false;
    }
    if (enabled && config.sub_stream.enabled &&
        !IsLdcStreamSizeSupported(config.sub_stream)) {
        Error("hisi_vendor", "VPSS LDC sub stream size unsupported: %ux%u",
              config.sub_stream.size.width, config.sub_stream.size.height);
        return false;
    }
    const VPSS_GRP vpss_group = static_cast<VPSS_GRP>(config.vpss_group);
    if (!ApplyVpssChannelLdc(vpss_group,
                             static_cast<VPSS_CHN>(config.vpss_channel),
                             *lens_correction)) {
        return false;
    }
    if (config.sub_stream.enabled &&
        !ApplyVpssChannelLdc(
            vpss_group, static_cast<VPSS_CHN>(config.sub_vpss_channel),
            *lens_correction)) {
        return false;
    }
    return true;
}

DIS_MOTION_LEVEL_E ParseDisMotionLevel(const Json& stabilization) {
    std::string motion_level;
    if (!json_reader::ReadField(stabilization, "motion_level",
                                &motion_level)) {
        return DIS_MOTION_LEVEL_NORMAL;
    }
    if (motion_level == "low") {
        return DIS_MOTION_LEVEL_LOW;
    }
    if (motion_level == "high") {
        return DIS_MOTION_LEVEL_HIGH;
    }
    return DIS_MOTION_LEVEL_NORMAL;
}

bool IsDisStreamSizeSupported(const VideoStreamConfig& stream_config) {
    return stream_config.size.width >= DIS_MIN_IMAGE_WIDTH &&
           stream_config.size.height >= DIS_MIN_IMAGE_HEIGHT;
}

bool ApplyDisAttr(VI_PIPE vi_pipe, VI_CHN vi_channel,
                  const DIS_ATTR_S& dis_attr) {
    const HI_S32 status =
        HI_MPI_VI_SetChnDISAttr(vi_pipe, vi_channel, &dis_attr);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VI_SetChnDISAttr pipe=%d chn=%d failed: 0x%08x",
              vi_pipe, vi_channel, status);
        return false;
    }
    return true;
}

bool ApplyStabilization(MppHisiSdkImpl& impl,
                        const MediaPipelineConfig& config,
                        const Json& image_config) {
    const Json* stabilization = nullptr;
    Json disabled_stabilization = Json::object();
    if (!FindSection(image_config, "stabilization", &stabilization)) {
        disabled_stabilization["enabled"] = false;
        stabilization = &disabled_stabilization;
    }
    bool enabled = false;
    if (!json_reader::ReadField(*stabilization, "enabled", &enabled)) {
        return false;
    }
    if (enabled && !IsDisStreamSizeSupported(config.main_stream)) {
        Error("hisi_vendor", "VI DIS main stream size unsupported: %ux%u",
              config.main_stream.size.width, config.main_stream.size.height);
        return false;
    }
    if (enabled && config.sub_stream.enabled &&
        !IsDisStreamSizeSupported(config.sub_stream)) {
        Error("hisi_vendor", "VI DIS sub stream size unsupported: %ux%u",
              config.sub_stream.size.width, config.sub_stream.size.height);
        return false;
    }

    DIS_CONFIG_S dis_config{};
    dis_config.enMode = DIS_MODE_4_DOF_GME;
    dis_config.enMotionLevel = ParseDisMotionLevel(*stabilization);
    dis_config.enPdtType = DIS_PDT_TYPE_IPC;
    dis_config.u32BufNum = 6;
    dis_config.u32CropRatio = 80;
    dis_config.u32FrameRate =
        static_cast<HI_U32>(config.main_stream.frame_rate.target_fps);
    dis_config.bCameraSteady = HI_FALSE;
    dis_config.bScale = HI_TRUE;

    int32_t value = 0;
    if (json_reader::ReadField(*stabilization, "buffer_frames", &value,
                               kDisBufferSizeMin, kDisBufferSizeMax)) {
        dis_config.u32BufNum = static_cast<HI_U32>(value);
    }
    if (json_reader::ReadField(*stabilization, "crop_ratio", &value,
                               kDisCropRatioMin, kDisCropRatioMax)) {
        dis_config.u32CropRatio = static_cast<HI_U32>(value);
    }
    if (json_reader::ReadField(*stabilization, "frame_rate", &value,
                               kDisFrameRateMin, kDisFrameRateMax)) {
        dis_config.u32FrameRate = static_cast<HI_U32>(value);
    }

    DIS_ATTR_S dis_attr{};
    dis_attr.bEnable = enabled ? HI_TRUE : HI_FALSE;
    dis_attr.bGdcBypass = HI_FALSE;
    dis_attr.u32MovingSubjectLevel = 0;
    dis_attr.s32RollingShutterCoef = 0;
    dis_attr.u32HorizontalLimit = 512;
    dis_attr.u32VerticalLimit = 512;
    dis_attr.bStillCrop = HI_FALSE;

    if (json_reader::ReadField(*stabilization, "moving_subject_level", &value,
                               kDisMovingSubjectLevelMin,
                               kDisMovingSubjectLevelMax)) {
        dis_attr.u32MovingSubjectLevel = static_cast<HI_U32>(value);
    }
    if (json_reader::ReadField(*stabilization, "rolling_shutter_coef", &value,
                               kDisRollingShutterCoefMin,
                               kDisRollingShutterCoefMax)) {
        dis_attr.s32RollingShutterCoef = value;
    }
    if (json_reader::ReadField(*stabilization, "horizontal_limit", &value,
                               kDisDriftLimitMin, kDisDriftLimitMax)) {
        dis_attr.u32HorizontalLimit = static_cast<HI_U32>(value);
    }
    if (json_reader::ReadField(*stabilization, "vertical_limit", &value,
                               kDisDriftLimitMin, kDisDriftLimitMax)) {
        dis_attr.u32VerticalLimit = static_cast<HI_U32>(value);
    }

    const VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    const VI_CHN vi_channel = static_cast<VI_CHN>(config.vi_channel);
    if (!enabled) {
        if (!impl.dis_enabled_) {
            return true;
        }
        if (!ApplyDisAttr(vi_pipe, vi_channel, dis_attr)) {
            return false;
        }
        impl.dis_enabled_ = false;
        return true;
    }

    HI_S32 status =
        HI_MPI_VI_SetChnDISConfig(vi_pipe, vi_channel, &dis_config);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VI_SetChnDISConfig pipe=%d chn=%d failed: 0x%08x",
              vi_pipe, vi_channel, status);
        return false;
    }
    if (!ApplyDisAttr(vi_pipe, vi_channel, dis_attr)) {
        return false;
    }
    impl.dis_enabled_ = true;
    return true;
}

}  // namespace

ImageTransformControls::ImageTransformControls(
    MppHisiSdkImpl& impl,
    const MediaPipelineConfig& config)
    : impl_(impl), config_(config) {}

bool ImageTransformControls::Apply(const Json& image_config) {
    return ApplyLensCorrection(config_, image_config) &&
           ApplyStabilization(impl_, config_, image_config);
}

}  // namespace hisisdk
}  // namespace live_stream
