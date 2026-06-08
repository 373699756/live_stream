#include "venc_config.h"

#include "infra/clamp.h"

namespace live_stream {
namespace hisisdk {
namespace venc_internal {
namespace {

constexpr uint32_t kDefaultStatTimeSec = 1;
constexpr int32_t kSmartPBgQpDelta = 6;
constexpr int32_t kSmartPViQpDelta = 2;
constexpr uint32_t kSmartPBgIntervalMultiplier = 4;
constexpr int32_t kVbrChangePos = 85;
constexpr uint32_t kVbrMinIprop = 1;
constexpr uint32_t kVbrMaxIprop = 100;
constexpr uint32_t kVbrMinQp = 10;
constexpr uint32_t kVbrMaxQp = 48;
constexpr uint32_t kVbrMinIQp = 10;
constexpr uint32_t kVbrMaxIQp = 42;
constexpr uint32_t kMinRcStatTimeSec = 1;
constexpr uint32_t kMaxRcStatTimeSec = 60;
constexpr uint32_t kMinRcBitrateKbps = 2;
constexpr uint32_t kMaxRcBitrateKbps = 614400;
constexpr uint32_t kMaxInputFrameRate = 240;

void CloseReEncode(VENC_RC_PARAM_S *rc_param, VENC_RC_MODE_E rc_mode) {
    if (rc_param == nullptr) {
        return;
    }
    switch (rc_mode) {
        case VENC_RC_MODE_H264CBR:
            rc_param->stParamH264Cbr.s32MaxReEncodeTimes = 0;
            break;
        case VENC_RC_MODE_H264VBR:
            rc_param->stParamH264Vbr.s32MaxReEncodeTimes = 0;
            break;
        case VENC_RC_MODE_H265CBR:
            rc_param->stParamH265Cbr.s32MaxReEncodeTimes = 0;
            break;
        case VENC_RC_MODE_H265VBR:
            rc_param->stParamH265Vbr.s32MaxReEncodeTimes = 0;
            break;
        default:
            break;
    }
}

void TuneH264VbrParam(VENC_PARAM_H264_VBR_S *param) {
    if (param == nullptr) {
        return;
    }
    param->s32ChangePos = kVbrChangePos;
    param->u32MinIprop = kVbrMinIprop;
    param->u32MaxIprop = kVbrMaxIprop;
    param->u32MinQp = kVbrMinQp;
    param->u32MaxQp = kVbrMaxQp;
    param->u32MinIQp = kVbrMinIQp;
    param->u32MaxIQp = kVbrMaxIQp;
    param->s32MaxReEncodeTimes = 0;
    param->bQpMapEn = HI_FALSE;
}

void TuneH265VbrParam(VENC_PARAM_H265_VBR_S *param) {
    if (param == nullptr) {
        return;
    }
    param->s32ChangePos = kVbrChangePos;
    param->u32MinIprop = kVbrMinIprop;
    param->u32MaxIprop = kVbrMaxIprop;
    param->u32MinQp = kVbrMinQp;
    param->u32MaxQp = kVbrMaxQp;
    param->u32MinIQp = kVbrMinIQp;
    param->u32MaxIQp = kVbrMaxIQp;
    param->s32MaxReEncodeTimes = 0;
    param->bQpMapEn = HI_FALSE;
}

}  // namespace

PAYLOAD_TYPE_E PayloadFromCodec(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::kH264:
            return PT_H264;
        case VideoCodec::kH265:
            return PT_H265;
        case VideoCodec::kMjpeg:
            return PT_MJPEG;
        case VideoCodec::kJpeg:
            return PT_JPEG;
    }
    return PT_H265;
}

VENC_RC_MODE_E RcModeFromConfig(VideoCodec codec, RateControlMode mode) {
    switch (codec) {
        case VideoCodec::kH264:
            switch (mode) {
                case RateControlMode::kCbr:
                    return VENC_RC_MODE_H264CBR;
                case RateControlMode::kVbr:
                    return VENC_RC_MODE_H264VBR;
                case RateControlMode::kFixQp:
                    return VENC_RC_MODE_H264FIXQP;
            }
            break;
        case VideoCodec::kH265:
            switch (mode) {
                case RateControlMode::kCbr:
                    return VENC_RC_MODE_H265CBR;
                case RateControlMode::kVbr:
                    return VENC_RC_MODE_H265VBR;
                case RateControlMode::kFixQp:
                    return VENC_RC_MODE_H265FIXQP;
            }
            break;
        case VideoCodec::kMjpeg:
            switch (mode) {
                case RateControlMode::kCbr:
                    return VENC_RC_MODE_MJPEGCBR;
                case RateControlMode::kVbr:
                    return VENC_RC_MODE_MJPEGVBR;
                case RateControlMode::kFixQp:
                    return VENC_RC_MODE_MJPEGFIXQP;
            }
            break;
        case VideoCodec::kJpeg:
            return VENC_RC_MODE_H264CBR;
    }
    return VENC_RC_MODE_H264CBR;
}

VENC_GOP_ATTR_S GopAttrFromConfig(GopMode mode, uint32_t gop) {
    VENC_GOP_ATTR_S attr{};
    switch (mode) {
        case GopMode::kDualP:
            attr.enGopMode = VENC_GOPMODE_DUALP;
            attr.stDualP.s32IPQpDelta = 4;
            attr.stDualP.s32SPQpDelta = 2;
            attr.stDualP.u32SPInterval = gop > 3 ? 3 : 0;
            break;
        case GopMode::kSmartP:
            attr.enGopMode = VENC_GOPMODE_SMARTP;
            attr.stSmartP.s32BgQpDelta = kSmartPBgQpDelta;
            attr.stSmartP.s32ViQpDelta = kSmartPViQpDelta;
            attr.stSmartP.u32BgInterval =
                gop > 0 ? gop * kSmartPBgIntervalMultiplier : 90;
            break;
        case GopMode::kNormalP:
            attr.enGopMode = VENC_GOPMODE_NORMALP;
            attr.stNormalP.s32IPQpDelta = 2;
            break;
    }
    return attr;
}

uint32_t StatTimeFromConfig(const VENC_GOP_ATTR_S &gop_attr, uint32_t gop) {
    if (gop_attr.enGopMode == VENC_GOPMODE_SMARTP && gop > 0) {
        const uint32_t stat_time = gop_attr.stSmartP.u32BgInterval / gop;
        if (stat_time >= kMinRcStatTimeSec && stat_time <= kMaxRcStatTimeSec) {
            return stat_time;
        }
    }
    return kDefaultStatTimeSec;
}

const char *CodecName(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::kH264:
            return "h264";
        case VideoCodec::kH265:
            return "h265";
        case VideoCodec::kMjpeg:
            return "mjpeg";
        case VideoCodec::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

const char *RcModeName(RateControlMode mode) {
    switch (mode) {
        case RateControlMode::kCbr:
            return "cbr";
        case RateControlMode::kVbr:
            return "vbr";
        case RateControlMode::kFixQp:
            return "fixqp";
    }
    return "unknown";
}

const char *GopModeName(GopMode mode) {
    switch (mode) {
        case GopMode::kNormalP:
            return "normal_p";
        case GopMode::kDualP:
            return "dual_p";
        case GopMode::kSmartP:
            return "smart_p";
    }
    return "unknown";
}

bool IsIdrCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

bool ValidateVencStreamConfig(int32_t chn, const VideoStreamConfig &stream) {
    if (chn < 0 || stream.size.width == 0 || stream.size.height == 0 ||
        stream.gop == 0 || stream.frame_rate.source_fps <= 0 ||
        stream.frame_rate.target_fps <= 0 ||
        stream.frame_rate.target_fps > stream.frame_rate.source_fps ||
        stream.frame_rate.source_fps >
            static_cast<int32_t>(kMaxInputFrameRate)) {
        Error(
            "hisi_vendor",
            "invalid VENC config chn=%d codec=%s %ux%u src_fps=%d "
            "dst_fps=%d gop=%u",
            chn, CodecName(stream.codec), stream.size.width,
            stream.size.height, stream.frame_rate.source_fps,
            stream.frame_rate.target_fps, stream.gop);
        return false;
    }
    if (stream.rc_mode != RateControlMode::kFixQp &&
        (stream.bitrate_kbps < kMinRcBitrateKbps ||
         stream.bitrate_kbps > kMaxRcBitrateKbps)) {
        Error("hisi_vendor",
              "invalid VENC bitrate chn=%d codec=%s rc=%s bitrate=%u", chn,
              CodecName(stream.codec), RcModeName(stream.rc_mode),
              stream.bitrate_kbps);
        return false;
    }
    if (stream.codec == VideoCodec::kJpeg) {
        Error("hisi_vendor", "JPEG VENC stream mode is not supported chn=%d",
              chn);
        return false;
    }
    return true;
}

bool TuneRcParam(VENC_CHN venc, VENC_RC_MODE_E rc_mode) {
    VENC_RC_PARAM_S rc_param{};
    if (!MpiOk("HI_MPI_VENC_GetRcParam",
               HI_MPI_VENC_GetRcParam(venc, &rc_param))) {
        return false;
    }

    CloseReEncode(&rc_param, rc_mode);
    if (rc_mode == VENC_RC_MODE_H264VBR) {
        TuneH264VbrParam(&rc_param.stParamH264Vbr);
    } else if (rc_mode == VENC_RC_MODE_H265VBR) {
        TuneH265VbrParam(&rc_param.stParamH265Vbr);
    }

    return MpiOk("HI_MPI_VENC_SetRcParam",
                 HI_MPI_VENC_SetRcParam(venc, &rc_param));
}

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream
