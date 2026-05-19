#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include "live_stream/json_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

constexpr int32_t kConfigMax = 100;
constexpr int32_t kConfigNeutral = kConfigMax / 2;
constexpr uint16_t kGainBase = 0x400;
constexpr uint16_t kWbGainMin = 0x200;
constexpr uint16_t kWbGainMax = 0x800;

bool CheckMpiCall(const char* expression, HI_S32 status) {
    if (status == HI_SUCCESS) {
        return true;
    }
    INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", expression, status);
    return false;
}

uint32_t ScaleControl(int32_t value, uint32_t min_value, uint32_t max_value) {
    const int32_t clamped = std::max(0, std::min(kConfigMax, value));
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
    const int32_t clamped = std::max(0, std::min(kConfigMax, value));
    if (clamped <= kConfigNeutral) {
        return ScaleControlU16(clamped * 2, kWbGainMin, kGainBase);
    }
    return ScaleControlU16((clamped - kConfigNeutral) * 2, kGainBase,
                           kWbGainMax);
}

bool LoadSection(const ConfigJson& image_config, const char* section_name,
                 const ConfigJson** section) {
    return json_utils::LoadObject(image_config, section_name, section);
}

bool LoadInt(const ConfigJson& section, const char* key, int32_t* value) {
    return json_utils::Load(section, key, value, 0, kConfigMax);
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
    if (!json_utils::Load(exposure, "anti_flicker", &anti_flicker)) {
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
    if (!CheckMpiCall("HI_MPI_ISP_GetExposureAttr",
                      HI_MPI_ISP_GetExposureAttr(vi_pipe, &attr))) {
        return false;
    }

    std::string mode;
    if (json_utils::Load(exposure, "mode", &mode)) {
        attr.enOpType = mode == "manual" ? OP_TYPE_MANUAL : OP_TYPE_AUTO;
    }

    int32_t compensation = 0;
    if (LoadInt(exposure, "compensation", &compensation)) {
        attr.stAuto.u8Compensation =
            ScaleControlU8(compensation, 0x00, 0xff);
    }

    bool slow_shutter = false;
    if (json_utils::Load(exposure, "slow_shutter", &slow_shutter)) {
        attr.stAuto.enAEMode =
            slow_shutter ? AE_MODE_SLOW_SHUTTER : AE_MODE_FIX_FRAME_RATE;
    }

    ApplyAntiFlicker(exposure, &attr);

    std::string exposure_time;
    if (json_utils::Load(exposure, "exposure_time", &exposure_time)) {
        uint32_t exposure_us = 0;
        if (ParseExposureTimeUs(exposure_time, &exposure_us)) {
            attr.stManual.enExpTimeOpType = OP_TYPE_MANUAL;
            attr.stManual.u32ExpTime = exposure_us;
        } else {
            attr.stManual.enExpTimeOpType = OP_TYPE_AUTO;
        }
    }

    std::string gain;
    if (json_utils::Load(exposure, "gain", &gain)) {
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

    return CheckMpiCall("HI_MPI_ISP_SetExposureAttr",
                        HI_MPI_ISP_SetExposureAttr(vi_pipe, &attr));
}

bool ApplySaturation(VI_PIPE vi_pipe, const ConfigJson& basic) {
    int32_t saturation = 0;
    if (!LoadInt(basic, "saturation", &saturation)) {
        return true;
    }
    ISP_SATURATION_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetSaturationAttr",
                      HI_MPI_ISP_GetSaturationAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.enOpType = OP_TYPE_MANUAL;
    attr.stManual.u8Saturation = ScaleControlU8(saturation, 0, 0xff);
    return CheckMpiCall("HI_MPI_ISP_SetSaturationAttr",
                        HI_MPI_ISP_SetSaturationAttr(vi_pipe, &attr));
}

bool ApplySharpen(VI_PIPE vi_pipe, const ConfigJson& basic) {
    int32_t sharpness = 0;
    if (!LoadInt(basic, "sharpness", &sharpness)) {
        return true;
    }
    ISP_SHARPEN_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetIspSharpenAttr",
                      HI_MPI_ISP_GetIspSharpenAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = HI_TRUE;
    attr.enOpType = OP_TYPE_MANUAL;
    const uint16_t texture = ScaleControlU16(sharpness, 0, 0x0fff);
    const uint16_t edge = ScaleControlU16(sharpness, 0, 0x0fff);
    std::fill(std::begin(attr.stManual.au16TextureStr),
              std::end(attr.stManual.au16TextureStr), texture);
    std::fill(std::begin(attr.stManual.au16EdgeStr),
              std::end(attr.stManual.au16EdgeStr), edge);
    attr.stManual.u16TextureFreq = ScaleControlU16(sharpness, 0, 0x0fff);
    attr.stManual.u16EdgeFreq = ScaleControlU16(sharpness, 0, 0x0fff);
    attr.stManual.u8OverShoot = ScaleControlU8(sharpness, 0, 127);
    attr.stManual.u8UnderShoot = ScaleControlU8(sharpness, 0, 127);
    return CheckMpiCall("HI_MPI_ISP_SetIspSharpenAttr",
                        HI_MPI_ISP_SetIspSharpenAttr(vi_pipe, &attr));
}

bool ApplyWhiteBalance(VI_PIPE vi_pipe, const ConfigJson& white_balance) {
    ISP_WB_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetWBAttr",
                      HI_MPI_ISP_GetWBAttr(vi_pipe, &attr))) {
        return false;
    }

    std::string mode;
    if (json_utils::Load(white_balance, "mode", &mode)) {
        attr.enOpType = mode == "manual" ? OP_TYPE_MANUAL : OP_TYPE_AUTO;
    }

    int32_t red_gain = 0;
    int32_t blue_gain = 0;
    if (LoadInt(white_balance, "red_gain", &red_gain)) {
        attr.stManual.u16Rgain = WhiteBalanceGainFromControl(red_gain);
    }
    if (LoadInt(white_balance, "blue_gain", &blue_gain)) {
        attr.stManual.u16Bgain = WhiteBalanceGainFromControl(blue_gain);
    }
    attr.stManual.u16Grgain = kGainBase;
    attr.stManual.u16Gbgain = kGainBase;

    return CheckMpiCall("HI_MPI_ISP_SetWBAttr",
                        HI_MPI_ISP_SetWBAttr(vi_pipe, &attr));
}

bool ApplyNoiseReduction(VI_PIPE vi_pipe, const ConfigJson& enhancement) {
    int32_t denoise_2d = 0;
    int32_t denoise_3d = 0;
    const bool has_2d = LoadInt(enhancement, "denoise_2d", &denoise_2d);
    const bool has_3d = LoadInt(enhancement, "denoise_3d", &denoise_3d);
    if (!has_2d && !has_3d) {
        return true;
    }

    ISP_NR_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetNRAttr",
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
        const uint16_t coarse = ScaleControlU16(denoise_3d, 0, 0x0360);
        std::fill(std::begin(attr.stManual.au16CoarseStr),
                  std::end(attr.stManual.au16CoarseStr), coarse);
    }
    return CheckMpiCall("HI_MPI_ISP_SetNRAttr",
                        HI_MPI_ISP_SetNRAttr(vi_pipe, &attr));
}

bool ApplyGamma(VI_PIPE vi_pipe, const ConfigJson& enhancement) {
    int32_t gamma = 0;
    if (!LoadInt(enhancement, "gamma", &gamma)) {
        return true;
    }
    ISP_GAMMA_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetGammaAttr",
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
        const double mapped = std::pow(normalized, exponent);
        attr.u16Table[i] =
            static_cast<HI_U16>(std::max(0.0, std::min(4095.0, mapped * 4095.0)));
    }
    return CheckMpiCall("HI_MPI_ISP_SetGammaAttr",
                        HI_MPI_ISP_SetGammaAttr(vi_pipe, &attr));
}

bool ApplyDehaze(VI_PIPE vi_pipe, const ConfigJson& enhancement) {
    bool defog = false;
    if (!json_utils::Load(enhancement, "defog", &defog)) {
        return true;
    }
    ISP_DEHAZE_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetDehazeAttr",
                      HI_MPI_ISP_GetDehazeAttr(vi_pipe, &attr))) {
        return false;
    }
    attr.bEnable = defog ? HI_TRUE : HI_FALSE;
    attr.enOpType = OP_TYPE_AUTO;
    attr.stAuto.u8strength = defog ? 128 : 0;
    return CheckMpiCall("HI_MPI_ISP_SetDehazeAttr",
                        HI_MPI_ISP_SetDehazeAttr(vi_pipe, &attr));
}

bool ApplyBacklight(VI_PIPE vi_pipe, const ConfigJson& backlight) {
    std::string mode;
    int32_t level = 0;
    const bool has_mode = json_utils::Load(backlight, "mode", &mode);
    const bool has_level = LoadInt(backlight, "level", &level);
    if (!has_mode && !has_level) {
        return true;
    }
    ISP_DRC_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_ISP_GetDRCAttr",
                      HI_MPI_ISP_GetDRCAttr(vi_pipe, &attr))) {
        return false;
    }
    if (has_mode) {
        attr.bEnable = mode == "off" ? HI_FALSE : HI_TRUE;
        if (mode != "off") {
            INFRA_LOG_WARN("hisi_vendor",
                           "backlight mode %s is applied as DRC strength only",
                           mode.c_str());
        }
    }
    if (has_level) {
        attr.enOpType = OP_TYPE_AUTO;
        attr.stAuto.u16Strength = ScaleControlU16(level, 0, 0x03ff);
        attr.stAuto.u16StrengthMin = 0;
        attr.stAuto.u16StrengthMax = 0x03ff;
    }
    return CheckMpiCall("HI_MPI_ISP_SetDRCAttr",
                        HI_MPI_ISP_SetDRCAttr(vi_pipe, &attr));
}

bool ApplyOrientation(VI_PIPE vi_pipe, VI_CHN vi_channel,
                      const ConfigJson& orientation) {
    bool mirror = false;
    bool flip = false;
    const bool has_mirror = json_utils::Load(orientation, "mirror", &mirror);
    const bool has_flip = json_utils::Load(orientation, "flip", &flip);
    if (!has_mirror && !has_flip) {
        return true;
    }
    VI_CHN_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_VI_GetChnAttr",
                      HI_MPI_VI_GetChnAttr(vi_pipe, vi_channel, &attr))) {
        return false;
    }
    if (has_mirror) {
        attr.bMirror = mirror ? HI_TRUE : HI_FALSE;
    }
    if (has_flip) {
        attr.bFlip = flip ? HI_TRUE : HI_FALSE;
    }
    return CheckMpiCall("HI_MPI_VI_SetChnAttr",
                        HI_MPI_VI_SetChnAttr(vi_pipe, vi_channel, &attr));
}

void LogUnsupportedControls(const ConfigJson& image_config) {
    const ConfigJson* basic = nullptr;
    if (LoadSection(image_config, "basic", &basic)) {
        if (basic->contains("brightness") || basic->contains("contrast") ||
            basic->contains("hue")) {
            INFRA_LOG_INFO(
                "hisi_vendor",
                "image brightness/contrast/hue are stored but not mapped to "
                "HiSilicon ISP in this build");
        }
    }
    const ConfigJson* color_mode = nullptr;
    if (LoadSection(image_config, "color_mode", &color_mode) &&
        color_mode->contains("mode")) {
        INFRA_LOG_INFO(
            "hisi_vendor",
            "image color_mode.mode is stored but not mapped to HiSilicon ISP "
            "in this build");
    }
}

}  // namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

bool MppHisiSdk::ApplyImageConfig(const MediaPipelineConfig& config,
                                  const ConfigJson& image_config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    if (!image_config.is_object()) {
        return false;
    }

    VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    VI_CHN vi_channel = static_cast<VI_CHN>(config.vi_channel);

    const ConfigJson* basic = nullptr;
    if (LoadSection(image_config, "basic", &basic) &&
        (!ApplySaturation(vi_pipe, *basic) ||
         !ApplySharpen(vi_pipe, *basic))) {
        return false;
    }

    const ConfigJson* exposure = nullptr;
    if (LoadSection(image_config, "exposure", &exposure) &&
        !ApplyExposure(vi_pipe, *exposure)) {
        return false;
    }

    const ConfigJson* white_balance = nullptr;
    if (LoadSection(image_config, "white_balance", &white_balance) &&
        !ApplyWhiteBalance(vi_pipe, *white_balance)) {
        return false;
    }

    const ConfigJson* enhancement = nullptr;
    if (LoadSection(image_config, "enhancement", &enhancement) &&
        (!ApplyNoiseReduction(vi_pipe, *enhancement) ||
         !ApplyGamma(vi_pipe, *enhancement) ||
         !ApplyDehaze(vi_pipe, *enhancement))) {
        return false;
    }

    const ConfigJson* backlight = nullptr;
    if (LoadSection(image_config, "backlight", &backlight) &&
        !ApplyBacklight(vi_pipe, *backlight)) {
        return false;
    }

    const ConfigJson* orientation = nullptr;
    if (LoadSection(image_config, "orientation", &orientation) &&
        !ApplyOrientation(vi_pipe, vi_channel, *orientation)) {
        return false;
    }

    LogUnsupportedControls(image_config);
    return true;
#else
    (void)config;
    return image_config.is_object();
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
