#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include "infra/clamp.h"
#include "json_utils.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace live_stream {
namespace hisisdk {

namespace {

constexpr int32_t kConfigMax = 100;
constexpr int32_t kConfigNeutral = kConfigMax / 2;
constexpr uint16_t kGainBase = 0x400;
constexpr uint16_t kWbGainMin = 0x200;
constexpr uint16_t kWbGainMax = 0x800;
constexpr uint16_t kSharpenStrengthMax = 0x0600;
constexpr uint16_t kSharpenFreqMax = 0x0800;
constexpr uint8_t kSharpenShootMax = 48;
constexpr uint16_t kNrCoarseMax = 0x02c0;

uint32_t ScaleControl(int32_t value, uint32_t min_value, uint32_t max_value) {
    const int32_t clamped = infra::Clamp(value, 0, kConfigMax);
    return min_value +
           (static_cast<uint32_t>(clamped) * (max_value - min_value) +
            kConfigMax / 2) /
               kConfigMax;
}

uint8_t ScaleControlU8(int32_t value, uint8_t min_value, uint8_t max_value) {
    return static_cast<uint8_t>(ScaleControl(value, min_value, max_value));
}

uint16_t ScaleControlU16(int32_t value, uint16_t min_value,
                         uint16_t max_value) {
    return static_cast<uint16_t>(ScaleControl(value, min_value, max_value));
}

uint16_t WhiteBalanceGainFromControl(int32_t value) {
    const int32_t clamped = infra::Clamp(value, 0, kConfigMax);
    if (clamped <= kConfigNeutral) {
        return ScaleControlU16(clamped * 2, kWbGainMin, kGainBase);
    }
    return ScaleControlU16((clamped - kConfigNeutral) * 2, kGainBase,
                           kWbGainMax);
}

bool FindSection(const ConfigJson& image_config, const char* section_name,
                 const ConfigJson** section) {
    if (section == nullptr || section_name == nullptr ||
        !image_config.contains(section_name) ||
        !image_config.at(section_name).is_object()) {
        return false;
    }
    *section = &image_config.at(section_name);
    return true;
}

bool ParseExposureTimeUs(const std::string& value, uint32_t* exposure_us) {
    if (exposure_us == nullptr || value == "auto") {
        return false;
    }
    if (value == "1/12") {
        *exposure_us = 83333;
        return true;
    }
    if (value == "1/25") {
        *exposure_us = 40000;
        return true;
    }
    if (value == "1/50") {
        *exposure_us = 20000;
        return true;
    }
    if (value == "1/100") {
        *exposure_us = 10000;
        return true;
    }
    if (value == "1/250") {
        *exposure_us = 4000;
        return true;
    }
    return false;
}

void ApplyAntiFlicker(const ConfigJson& exposure, ISP_EXPOSURE_ATTR_S* attr) {
    std::string anti_flicker;
    if (!json_utils::ReadField(exposure, "anti_flicker", &anti_flicker)) {
        return;
    }
    if (anti_flicker == "off") {
        attr->stAuto.stAntiflicker.bEnable = HI_FALSE;
    } else if (anti_flicker == "50hz") {
        attr->stAuto.stAntiflicker.bEnable = HI_TRUE;
        attr->stAuto.stAntiflicker.u8Frequency = 50;
    } else if (anti_flicker == "60hz") {
        attr->stAuto.stAntiflicker.bEnable = HI_TRUE;
        attr->stAuto.stAntiflicker.u8Frequency = 60;
    }
}

bool ApplyExposure(VI_PIPE vi_pipe, const ConfigJson& exposure) {
    ISP_EXPOSURE_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetExposureAttr",
               HI_MPI_ISP_GetExposureAttr(vi_pipe, &attr))) {
        return false;
    }

    std::string mode;
    if (json_utils::ReadField(exposure, "mode", &mode)) {
        attr.enOpType = mode == "manual" ? OP_TYPE_MANUAL : OP_TYPE_AUTO;
    }

    int32_t compensation = 0;
    if (json_utils::ReadField(exposure, "compensation", &compensation, 0,
                              kConfigMax)) {
        attr.stAuto.u8Compensation =
            ScaleControlU8(compensation, 0x00, 0xff);
    }

    bool slow_shutter = false;
    if (json_utils::ReadField(exposure, "slow_shutter", &slow_shutter)) {
        attr.stAuto.enAEMode =
            slow_shutter ? AE_MODE_SLOW_SHUTTER : AE_MODE_FIX_FRAME_RATE;
    }

    ApplyAntiFlicker(exposure, &attr);

    std::string exposure_time;
    if (json_utils::ReadField(exposure, "exposure_time", &exposure_time)) {
        uint32_t exposure_us = 0;
        if (ParseExposureTimeUs(exposure_time, &exposure_us)) {
            attr.stManual.enExpTimeOpType = OP_TYPE_MANUAL;
            attr.stManual.u32ExpTime = exposure_us;
        } else {
            attr.stManual.enExpTimeOpType = OP_TYPE_AUTO;
        }
    }

    std::string max_exposure_time;
    if (json_utils::ReadField(exposure, "max_exposure_time",
                              &max_exposure_time)) {
        uint32_t max_exposure_us = 0;
        if (ParseExposureTimeUs(max_exposure_time, &max_exposure_us)) {
            attr.stAuto.stExpTimeRange.u32Min = 0;
            attr.stAuto.stExpTimeRange.u32Max = max_exposure_us;
        }
    }

    std::string gain;
    if (json_utils::ReadField(exposure, "gain", &gain)) {
        if (gain == "auto") {
            attr.stManual.enAGainOpType = OP_TYPE_AUTO;
            attr.stManual.enDGainOpType = OP_TYPE_AUTO;
            attr.stManual.enISPDGainOpType = OP_TYPE_AUTO;
        } else {
            uint32_t gain_value = kGainBase;
            if (gain == "low") {
                gain_value = kGainBase * 2;
            } else if (gain == "medium") {
                gain_value = kGainBase * 4;
            } else if (gain == "high") {
                gain_value = kGainBase * 8;
            }
            attr.stManual.enAGainOpType = OP_TYPE_MANUAL;
            attr.stManual.u32AGain = gain_value;
        }
    }

    return MpiOk("HI_MPI_ISP_SetExposureAttr",
                 HI_MPI_ISP_SetExposureAttr(vi_pipe, &attr));
}

bool ApplyCsc(VI_PIPE vi_pipe, const ConfigJson& basic) {
    int32_t brightness = 0;
    int32_t contrast = 0;
    int32_t saturation = 0;
    int32_t hue = 0;
    const bool has_brightness = json_utils::ReadField(
        basic, "brightness", &brightness, 0, kConfigMax);
    const bool has_contrast = json_utils::ReadField(
        basic, "contrast", &contrast, 0, kConfigMax);
    const bool has_saturation = json_utils::ReadField(
        basic, "saturation", &saturation, 0, kConfigMax);
    const bool has_hue = json_utils::ReadField(
        basic, "hue", &hue, 0, kConfigMax);
    if (!has_brightness && !has_contrast && !has_saturation && !has_hue) {
        return true;
    }
    ISP_CSC_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetCSCAttr",
               HI_MPI_ISP_GetCSCAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = HI_TRUE;
    if (has_brightness) {
        attr.u8Luma = ScaleControlU8(brightness, 0, 100);
    }
    if (has_contrast) {
        attr.u8Contr = ScaleControlU8(contrast, 0, 100);
    }
    if (has_saturation) {
        attr.u8Satu = ScaleControlU8(saturation, 0, 100);
    }
    if (has_hue) {
        attr.u8Hue = ScaleControlU8(hue, 0, 100);
    }
    return MpiOk("HI_MPI_ISP_SetCSCAttr",
                 HI_MPI_ISP_SetCSCAttr(vi_pipe, &attr));
}

bool ApplySharpen(VI_PIPE vi_pipe, const ConfigJson& basic) {
    int32_t sharpness = 0;
    if (!json_utils::ReadField(basic, "sharpness", &sharpness, 0,
                               kConfigMax)) {
        return true;
    }
    ISP_SHARPEN_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetIspSharpenAttr",
               HI_MPI_ISP_GetIspSharpenAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = HI_TRUE;
    attr.enOpType = OP_TYPE_MANUAL;
    const uint16_t texture = ScaleControlU16(sharpness, 0, kSharpenStrengthMax);
    const uint16_t edge = ScaleControlU16(sharpness, 0, kSharpenStrengthMax);
    std::fill(std::begin(attr.stManual.au16TextureStr),
              std::end(attr.stManual.au16TextureStr), texture);
    std::fill(std::begin(attr.stManual.au16EdgeStr),
              std::end(attr.stManual.au16EdgeStr), edge);
    attr.stManual.u16TextureFreq = ScaleControlU16(sharpness, 0, kSharpenFreqMax);
    attr.stManual.u16EdgeFreq = ScaleControlU16(sharpness, 0, kSharpenFreqMax);
    attr.stManual.u8OverShoot = ScaleControlU8(sharpness, 0, kSharpenShootMax);
    attr.stManual.u8UnderShoot = ScaleControlU8(sharpness, 0, kSharpenShootMax);
    return MpiOk("HI_MPI_ISP_SetIspSharpenAttr",
                 HI_MPI_ISP_SetIspSharpenAttr(vi_pipe, &attr));
}

bool ApplyWhiteBalance(VI_PIPE vi_pipe, const ConfigJson& white_balance) {
    ISP_WB_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetWBAttr",
               HI_MPI_ISP_GetWBAttr(vi_pipe, &attr))) {
        return false;
    }

    std::string mode;
    if (json_utils::ReadField(white_balance, "mode", &mode)) {
        attr.enOpType = mode == "manual" ? OP_TYPE_MANUAL : OP_TYPE_AUTO;
    }

    int32_t red_gain = 0;
    int32_t blue_gain = 0;
    if (json_utils::ReadField(white_balance, "red_gain", &red_gain, 0,
                              kConfigMax)) {
        attr.stManual.u16Rgain = WhiteBalanceGainFromControl(red_gain);
    }
    if (json_utils::ReadField(white_balance, "blue_gain", &blue_gain, 0,
                              kConfigMax)) {
        attr.stManual.u16Bgain = WhiteBalanceGainFromControl(blue_gain);
    }
    attr.stManual.u16Grgain = kGainBase;
    attr.stManual.u16Gbgain = kGainBase;

    return MpiOk("HI_MPI_ISP_SetWBAttr",
                 HI_MPI_ISP_SetWBAttr(vi_pipe, &attr));
}

bool ApplyNoiseReduction(VI_PIPE vi_pipe, const ConfigJson& enhancement) {
    int32_t denoise_2d = 0;
    int32_t denoise_3d = 0;
    const bool has_2d = json_utils::ReadField(
        enhancement, "denoise_2d", &denoise_2d, 0, kConfigMax);
    const bool has_3d = json_utils::ReadField(
        enhancement, "denoise_3d", &denoise_3d, 0, kConfigMax);
    if (!has_2d && !has_3d) {
        return true;
    }

    ISP_NR_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetNRAttr",
               HI_MPI_ISP_GetNRAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = HI_TRUE;
    attr.enOpType = OP_TYPE_MANUAL;
    if (has_2d) {
        const uint8_t chroma = ScaleControlU8(denoise_2d, 0, 3);
        std::fill(std::begin(attr.stManual.au8ChromaStr),
                  std::end(attr.stManual.au8ChromaStr), chroma);
        attr.stManual.u8FineStr = ScaleControlU8(denoise_2d, 0, 0x80);
    }
    if (has_3d) {
        const uint16_t coarse = ScaleControlU16(denoise_3d, 0, kNrCoarseMax);
        std::fill(std::begin(attr.stManual.au16CoarseStr),
                  std::end(attr.stManual.au16CoarseStr), coarse);
    }
    return MpiOk("HI_MPI_ISP_SetNRAttr",
                 HI_MPI_ISP_SetNRAttr(vi_pipe, &attr));
}

bool ApplyGamma(VI_PIPE vi_pipe, const ConfigJson& enhancement) {
    int32_t gamma = 0;
    if (!json_utils::ReadField(enhancement, "gamma", &gamma, 0,
                               kConfigMax)) {
        return true;
    }
    ISP_GAMMA_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetGammaAttr",
               HI_MPI_ISP_GetGammaAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = HI_TRUE;
    attr.enCurveType = gamma == 50 ? ISP_GAMMA_CURVE_DEFAULT
                                   : ISP_GAMMA_CURVE_USER_DEFINE;
    const double exponent = 1.6 - static_cast<double>(gamma) * 0.012;
    for (uint32_t i = 0; i < GAMMA_NODE_NUM; ++i) {
        const double normalized =
            static_cast<double>(i) / static_cast<double>(GAMMA_NODE_NUM - 1);
        const double mapped = std::pow(normalized, exponent) * 4095.0;
        const double clamped = infra::Clamp(mapped, 0.0, 4095.0);
        attr.u16Table[i] = static_cast<HI_U16>(clamped);
    }
    return MpiOk("HI_MPI_ISP_SetGammaAttr",
                 HI_MPI_ISP_SetGammaAttr(vi_pipe, &attr));
}

bool ApplyDehaze(VI_PIPE vi_pipe, const ConfigJson& enhancement) {
    bool defog = false;
    if (!json_utils::ReadField(enhancement, "defog", &defog)) {
        return true;
    }
    ISP_DEHAZE_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetDehazeAttr",
               HI_MPI_ISP_GetDehazeAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = defog ? HI_TRUE : HI_FALSE;
    attr.enOpType = OP_TYPE_AUTO;
    attr.stAuto.u8strength = defog ? 128 : 0;
    return MpiOk("HI_MPI_ISP_SetDehazeAttr",
                 HI_MPI_ISP_SetDehazeAttr(vi_pipe, &attr));
}

bool ApplyBacklight(VI_PIPE vi_pipe, const ConfigJson& backlight) {
    std::string mode;
    int32_t level = 0;
    const bool has_mode = json_utils::ReadField(backlight, "mode", &mode);
    const bool has_level = json_utils::ReadField(backlight, "level", &level,
                                                 0, kConfigMax);
    if (!has_mode && !has_level) {
        return true;
    }
    ISP_DRC_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetDRCAttr",
               HI_MPI_ISP_GetDRCAttr(vi_pipe, &attr))) {
        return false;
    }
    if (has_mode) {
        if (mode == "off") {
            attr.bEnable = HI_FALSE;
        } else if (mode == "drc") {
            attr.bEnable = HI_TRUE;
        } else {
            INFRA_LOG_ERROR("hisi_vendor", "unsupported backlight mode %s",
                            mode.c_str());
            return false;
        }
    }
    if (has_level) {
        attr.enOpType = OP_TYPE_AUTO;
        attr.stAuto.u16Strength = ScaleControlU16(level, 0, 0x03ff);
        attr.stAuto.u16StrengthMin = 0;
        attr.stAuto.u16StrengthMax = 0x03ff;
    }
    return MpiOk("HI_MPI_ISP_SetDRCAttr",
                 HI_MPI_ISP_SetDRCAttr(vi_pipe, &attr));
}

bool ApplyOrientation(VI_PIPE vi_pipe, VI_CHN vi_channel,
                      const ConfigJson& orientation) {
    bool mirror = false;
    bool flip = false;
    const bool has_mirror = json_utils::ReadField(orientation, "mirror", &mirror);
    const bool has_flip = json_utils::ReadField(orientation, "flip", &flip);
    if (!has_mirror && !has_flip) {
        return true;
    }
    VI_CHN_ATTR_S attr{};
    if (!MpiOk("HI_MPI_VI_GetChnAttr",
               HI_MPI_VI_GetChnAttr(vi_pipe, vi_channel, &attr))) {
        return false;
    }
    if (has_mirror) {
        attr.bMirror = mirror ? HI_TRUE : HI_FALSE;
    }
    if (has_flip) {
        attr.bFlip = flip ? HI_TRUE : HI_FALSE;
    }
    return MpiOk("HI_MPI_VI_SetChnAttr",
                 HI_MPI_VI_SetChnAttr(vi_pipe, vi_channel, &attr));
}

bool ApplyColorMode(VI_PIPE vi_pipe, const ConfigJson& image_config) {
    const ConfigJson* color_mode = nullptr;
    if (!FindSection(image_config, "color_mode", &color_mode)) {
        return true;
    }
    std::string mode;
    if (!json_utils::ReadField(*color_mode, "mode", &mode)) {
        return true;
    }
    ISP_CSC_ATTR_S attr{};
    if (!MpiOk("HI_MPI_ISP_GetCSCAttr",
               HI_MPI_ISP_GetCSCAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = HI_TRUE;
    if (mode == "black_white") {
        attr.u8Satu = 0;
    } else if (mode == "color") {
        const ConfigJson* basic = nullptr;
        int32_t saturation = kConfigNeutral;
        if (FindSection(image_config, "basic", &basic)) {
            (void)json_utils::ReadField(*basic, "saturation", &saturation, 0,
                                        kConfigMax);
        }
        attr.u8Satu = ScaleControlU8(saturation, 0, 100);
    } else {
        return true;
    }
    return MpiOk("HI_MPI_ISP_SetCSCAttr",
                 HI_MPI_ISP_SetCSCAttr(vi_pipe, &attr));
}

}  // namespace

bool MppHisiSdk::ApplyImageConfig(const MediaPipelineConfig& config,
                                  const ConfigJson& image_config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (!image_config.is_object()) {
        return false;
    }

    VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    VI_CHN vi_channel = static_cast<VI_CHN>(config.vi_channel);

    const ConfigJson* basic = nullptr;
    if (FindSection(image_config, "basic", &basic) &&
        (!ApplyCsc(vi_pipe, *basic) ||
         !ApplySharpen(vi_pipe, *basic))) {
        return false;
    }

    const ConfigJson* exposure = nullptr;
    if (FindSection(image_config, "exposure", &exposure) &&
        !ApplyExposure(vi_pipe, *exposure)) {
        return false;
    }

    const ConfigJson* white_balance = nullptr;
    if (FindSection(image_config, "white_balance", &white_balance) &&
        !ApplyWhiteBalance(vi_pipe, *white_balance)) {
        return false;
    }

    const ConfigJson* enhancement = nullptr;
    if (FindSection(image_config, "enhancement", &enhancement) &&
        (!ApplyNoiseReduction(vi_pipe, *enhancement) ||
         !ApplyGamma(vi_pipe, *enhancement) ||
         !ApplyDehaze(vi_pipe, *enhancement))) {
        return false;
    }

    const ConfigJson* backlight = nullptr;
    if (FindSection(image_config, "backlight", &backlight) &&
        !ApplyBacklight(vi_pipe, *backlight)) {
        return false;
    }

    const ConfigJson* orientation = nullptr;
    if (FindSection(image_config, "orientation", &orientation) &&
        !ApplyOrientation(vi_pipe, vi_channel, *orientation)) {
        return false;
    }

    if (!ApplyColorMode(vi_pipe, image_config)) {
        return false;
    }
    return true;
}

ExposureInfo MppHisiSdk::QueryExposureInfo(
    const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    ExposureInfo info;
    if (!impl_->isp_started_) {
        return info;
    }

    ISP_EXP_INFO_S exp_info{};
    const VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    if (!MpiOk("HI_MPI_ISP_QueryExposureInfo",
               HI_MPI_ISP_QueryExposureInfo(vi_pipe, &exp_info))) {
        return info;
    }

    info.valid = true;
    info.exposure_time_us = exp_info.u32ExpTime;
    info.analog_gain = exp_info.u32AGain;
    info.digital_gain = exp_info.u32DGain;
    info.isp_digital_gain = exp_info.u32ISPDGain;
    info.iso = exp_info.u32ISO;
    return info;
}

}  // namespace hisisdk
}  // namespace live_stream
