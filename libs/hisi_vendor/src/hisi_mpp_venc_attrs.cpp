#include "hisi_mpp_venc_attrs.h"

#include "venc_config.h"

namespace live_stream {
namespace hisisdk {
namespace venc_internal {
namespace {

constexpr uint32_t kDefaultFixQpI = 25;
constexpr uint32_t kDefaultFixQpP = 30;
constexpr uint32_t kDefaultFixQpB = 32;
constexpr uint32_t kDefaultMjpegQfactor = 95;

}  // namespace

bool VencChannelAttrs::Build(const VideoStreamConfig& stream) {
    attr_ = VENC_CHN_ATTR_S{};
    FillCommonAttrs(stream);
    if (stream.codec == Codec::kH264) {
        FillH264RcAttrs(stream);
    } else if (stream.codec == Codec::kH265) {
        FillH265RcAttrs(stream);
    } else if (stream.codec == Codec::kMjpeg) {
        FillMjpegRcAttrs(stream);
    } else {
        return false;
    }
    return true;
}

void VencChannelAttrs::FillCommonAttrs(const VideoStreamConfig& stream) {
    attr_.stVencAttr.enType = PayloadFromCodec(stream.codec);
    attr_.stVencAttr.u32MaxPicWidth = stream.size.width;
    attr_.stVencAttr.u32MaxPicHeight = stream.size.height;
    attr_.stVencAttr.u32PicWidth = stream.size.width;
    attr_.stVencAttr.u32PicHeight = stream.size.height;
    attr_.stVencAttr.u32BufSize = stream.size.width * stream.size.height * 2;
    attr_.stVencAttr.bByFrame = HI_TRUE;
    attr_.stVencAttr.u32Profile = 0;
    attr_.stGopAttr = GopAttrFromConfig(stream.gop_mode, stream.gop);
    stat_time_ = StatTimeFromConfig(attr_.stGopAttr, stream.gop);
    attr_.stRcAttr.enRcMode = RcModeFromConfig(stream.codec, stream.rc_mode);
}

void VencChannelAttrs::FillH264RcAttrs(const VideoStreamConfig& stream) {
    attr_.stVencAttr.stAttrH264e.bRcnRefShareBuf = HI_FALSE;
    if (stream.rc_mode == RateControlMode::kCbr) {
        attr_.stRcAttr.stH264Cbr.u32BitRate = stream.bitrate_kbps;
        attr_.stRcAttr.stH264Cbr.u32Gop = stream.gop;
        attr_.stRcAttr.stH264Cbr.u32StatTime = stat_time_;
        attr_.stRcAttr.stH264Cbr.u32SrcFrameRate =
            stream.frame_rate.source_fps;
        attr_.stRcAttr.stH264Cbr.fr32DstFrameRate =
            static_cast<HI_FR32>(stream.frame_rate.target_fps);
        return;
    }
    if (stream.rc_mode == RateControlMode::kVbr) {
        attr_.stRcAttr.stH264Vbr.u32MaxBitRate = stream.bitrate_kbps;
        attr_.stRcAttr.stH264Vbr.u32Gop = stream.gop;
        attr_.stRcAttr.stH264Vbr.u32StatTime = stat_time_;
        attr_.stRcAttr.stH264Vbr.u32SrcFrameRate =
            stream.frame_rate.source_fps;
        attr_.stRcAttr.stH264Vbr.fr32DstFrameRate =
            static_cast<HI_FR32>(stream.frame_rate.target_fps);
        return;
    }
    attr_.stRcAttr.stH264FixQp.u32Gop = stream.gop;
    attr_.stRcAttr.stH264FixQp.u32SrcFrameRate =
        stream.frame_rate.source_fps;
    attr_.stRcAttr.stH264FixQp.fr32DstFrameRate =
        static_cast<HI_FR32>(stream.frame_rate.target_fps);
    attr_.stRcAttr.stH264FixQp.u32IQp = kDefaultFixQpI;
    attr_.stRcAttr.stH264FixQp.u32PQp = kDefaultFixQpP;
    attr_.stRcAttr.stH264FixQp.u32BQp = kDefaultFixQpB;
}

void VencChannelAttrs::FillH265RcAttrs(const VideoStreamConfig& stream) {
    attr_.stVencAttr.stAttrH265e.bRcnRefShareBuf = HI_FALSE;
    if (stream.rc_mode == RateControlMode::kCbr) {
        attr_.stRcAttr.stH265Cbr.u32BitRate = stream.bitrate_kbps;
        attr_.stRcAttr.stH265Cbr.u32Gop = stream.gop;
        attr_.stRcAttr.stH265Cbr.u32StatTime = stat_time_;
        attr_.stRcAttr.stH265Cbr.u32SrcFrameRate =
            stream.frame_rate.source_fps;
        attr_.stRcAttr.stH265Cbr.fr32DstFrameRate =
            static_cast<HI_FR32>(stream.frame_rate.target_fps);
        return;
    }
    if (stream.rc_mode == RateControlMode::kVbr) {
        attr_.stRcAttr.stH265Vbr.u32MaxBitRate = stream.bitrate_kbps;
        attr_.stRcAttr.stH265Vbr.u32Gop = stream.gop;
        attr_.stRcAttr.stH265Vbr.u32StatTime = stat_time_;
        attr_.stRcAttr.stH265Vbr.u32SrcFrameRate =
            stream.frame_rate.source_fps;
        attr_.stRcAttr.stH265Vbr.fr32DstFrameRate =
            static_cast<HI_FR32>(stream.frame_rate.target_fps);
        return;
    }
    attr_.stRcAttr.stH265FixQp.u32Gop = stream.gop;
    attr_.stRcAttr.stH265FixQp.u32SrcFrameRate =
        stream.frame_rate.source_fps;
    attr_.stRcAttr.stH265FixQp.fr32DstFrameRate =
        static_cast<HI_FR32>(stream.frame_rate.target_fps);
    attr_.stRcAttr.stH265FixQp.u32IQp = kDefaultFixQpI;
    attr_.stRcAttr.stH265FixQp.u32PQp = kDefaultFixQpP;
    attr_.stRcAttr.stH265FixQp.u32BQp = kDefaultFixQpB;
}

void VencChannelAttrs::FillMjpegRcAttrs(const VideoStreamConfig& stream) {
    attr_.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attr_.stGopAttr.stNormalP.s32IPQpDelta = 0;
    if (stream.rc_mode == RateControlMode::kCbr) {
        attr_.stRcAttr.stMjpegCbr.u32BitRate = stream.bitrate_kbps;
        attr_.stRcAttr.stMjpegCbr.u32StatTime = stat_time_;
        attr_.stRcAttr.stMjpegCbr.u32SrcFrameRate =
            stream.frame_rate.source_fps;
        attr_.stRcAttr.stMjpegCbr.fr32DstFrameRate =
            static_cast<HI_FR32>(stream.frame_rate.target_fps);
        return;
    }
    if (stream.rc_mode == RateControlMode::kVbr) {
        attr_.stRcAttr.stMjpegVbr.u32MaxBitRate = stream.bitrate_kbps;
        attr_.stRcAttr.stMjpegVbr.u32StatTime = stat_time_;
        attr_.stRcAttr.stMjpegVbr.u32SrcFrameRate =
            stream.frame_rate.source_fps;
        attr_.stRcAttr.stMjpegVbr.fr32DstFrameRate =
            static_cast<HI_FR32>(stream.frame_rate.target_fps);
        return;
    }
    attr_.stRcAttr.stMjpegFixQp.u32SrcFrameRate =
        stream.frame_rate.source_fps;
    attr_.stRcAttr.stMjpegFixQp.fr32DstFrameRate =
        static_cast<HI_FR32>(stream.frame_rate.target_fps);
    attr_.stRcAttr.stMjpegFixQp.u32Qfactor = kDefaultMjpegQfactor;
}

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream
