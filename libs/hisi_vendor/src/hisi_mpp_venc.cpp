#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include "infra/clamp.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/select.h>
#include <sys/time.h>
#include <thread>
#include <utility>

namespace live_stream {
namespace hisisdk {

namespace {

constexpr uint32_t kDefaultStatTimeSec = 1;
constexpr uint32_t kDefaultFixQpI = 25;
constexpr uint32_t kDefaultFixQpP = 30;
constexpr uint32_t kDefaultFixQpB = 32;
constexpr uint32_t kDefaultMjpegQfactor = 95;
constexpr uint32_t kMinRcStatTimeSec = 1;
constexpr uint32_t kMaxRcStatTimeSec = 60;
constexpr uint32_t kMinRcBitrateKbps = 2;
constexpr uint32_t kMaxRcBitrateKbps = 614400;
constexpr uint32_t kMaxInputFrameRate = 240;

// ─── Payload type from VideoCodec ──────────────────────────────
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

// ─── RC mode mapping ───────────────────────────────────────────
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
            return VENC_RC_MODE_H264CBR;  // JPEG uses CBR by default
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
            attr.stSmartP.s32BgQpDelta = 4;
            attr.stSmartP.s32ViQpDelta = 2;
            attr.stSmartP.u32BgInterval = gop > 0 ? gop * 3 : 90;
            break;
        case GopMode::kNormalP:
            attr.enGopMode = VENC_GOPMODE_NORMALP;
            attr.stNormalP.s32IPQpDelta = 2;
            break;
    }
    return attr;
}

uint32_t StatTimeFromConfig(const VENC_GOP_ATTR_S& gop_attr, uint32_t gop) {
    if (gop_attr.enGopMode == VENC_GOPMODE_SMARTP && gop > 0) {
        const uint32_t stat_time = gop_attr.stSmartP.u32BgInterval / gop;
        if (stat_time >= kMinRcStatTimeSec && stat_time <= kMaxRcStatTimeSec) {
            return stat_time;
        }
    }
    return kDefaultStatTimeSec;
}

const char* CodecName(VideoCodec codec) {
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

const char* RcModeName(RateControlMode mode) {
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

const char* GopModeName(GopMode mode) {
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

bool ValidateVencStreamConfig(int32_t chn, const VideoStreamConfig& stream) {
    if (chn < 0 || stream.size.width == 0 || stream.size.height == 0 ||
        stream.gop == 0 || stream.frame_rate.source_fps <= 0 ||
        stream.frame_rate.target_fps <= 0 ||
        stream.frame_rate.target_fps > stream.frame_rate.source_fps ||
        stream.frame_rate.source_fps > static_cast<int32_t>(kMaxInputFrameRate)) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "invalid VENC config chn=%d codec=%s %ux%u src_fps=%d "
            "dst_fps=%d gop=%u",
            chn, CodecName(stream.codec), stream.size.width, stream.size.height,
            stream.frame_rate.source_fps, stream.frame_rate.target_fps,
            stream.gop);
        return false;
    }
    if (stream.rc_mode != RateControlMode::kFixQp &&
        (stream.bitrate_kbps < kMinRcBitrateKbps ||
         stream.bitrate_kbps > kMaxRcBitrateKbps)) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "invalid VENC bitrate chn=%d codec=%s rc=%s bitrate=%u",
            chn, CodecName(stream.codec), RcModeName(stream.rc_mode),
            stream.bitrate_kbps);
        return false;
    }
    if (stream.codec == VideoCodec::kJpeg) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "JPEG VENC stream mode is not supported chn=%d", chn);
        return false;
    }
    return true;
}

bool CheckMpiCall(const char* expression, HI_S32 status) {
    if (status == HI_SUCCESS) {
        return true;
    }
    INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", expression, status);
    return false;
}

void UpdateFrameTypeFromH264(H264E_NALU_TYPE_E type, FrameType* frame_type) {
    if (frame_type == nullptr || *frame_type == FrameType::kIdr) {
        return;
    }
    if (type == H264E_NALU_IDRSLICE) {
        *frame_type = FrameType::kIdr;
    } else if (type == H264E_NALU_ISLICE) {
        *frame_type = FrameType::kI;
    } else if (type == H264E_NALU_BSLICE && *frame_type != FrameType::kI) {
        *frame_type = FrameType::kB;
    }
}

void UpdateFrameTypeFromH265(H265E_NALU_TYPE_E type, FrameType* frame_type) {
    if (frame_type == nullptr || *frame_type == FrameType::kIdr) {
        return;
    }
    if (type == H265E_NALU_IDRSLICE) {
        *frame_type = FrameType::kIdr;
    } else if (type == H265E_NALU_ISLICE) {
        *frame_type = FrameType::kI;
    } else if (type == H265E_NALU_BSLICE && *frame_type != FrameType::kI) {
        *frame_type = FrameType::kB;
    }
}

void UpdateFrameTypeFromH264Pack(const VENC_PACK_S& pack,
                                 uint32_t data_num,
                                 FrameType* frame_type) {
    UpdateFrameTypeFromH264(pack.DataType.enH264EType, frame_type);
    for (uint32_t j = 0; frame_type != nullptr &&
                         *frame_type != FrameType::kIdr && j < data_num; ++j) {
        UpdateFrameTypeFromH264(pack.stPackInfo[j].u32PackType.enH264EType,
                                frame_type);
    }
}

void UpdateFrameTypeFromH265Pack(const VENC_PACK_S& pack,
                                 uint32_t data_num,
                                 FrameType* frame_type) {
    UpdateFrameTypeFromH265(pack.DataType.enH265EType, frame_type);
    for (uint32_t j = 0; frame_type != nullptr &&
                         *frame_type != FrameType::kIdr && j < data_num; ++j) {
        UpdateFrameTypeFromH265(pack.stPackInfo[j].u32PackType.enH265EType,
                                frame_type);
    }
}

FrameType FrameTypeFromStream(const VENC_STREAM_S& stream, VideoCodec codec) {
    if (codec == VideoCodec::kJpeg || codec == VideoCodec::kMjpeg) {
        return FrameType::kJpeg;
    }
    if (stream.pstPack == nullptr || stream.u32PackCount == 0) {
        return FrameType::kP;
    }

    FrameType frame_type = FrameType::kP;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        const VENC_PACK_S& pack = stream.pstPack[i];
        const uint32_t data_num =
            infra::Clamp<uint32_t>(pack.u32DataNum, 0U, 8U);
        if (codec == VideoCodec::kH264) {
            UpdateFrameTypeFromH264Pack(pack, data_num, &frame_type);
        } else if (codec == VideoCodec::kH265) {
            UpdateFrameTypeFromH265Pack(pack, data_num, &frame_type);
        }
        if (frame_type == FrameType::kIdr) {
            return frame_type;
        }
    }

    return frame_type;
}

bool StartRecvFrame(VENC_CHN venc) {
    VENC_RECV_PIC_PARAM_S recv_param{};
    recv_param.s32RecvPicNum = -1;
    return CheckMpiCall("HI_MPI_VENC_StartRecvFrame",
                        HI_MPI_VENC_StartRecvFrame(venc, &recv_param));
}

void StopRecvFrame(VENC_CHN venc) {
    (void)HI_MPI_VENC_StopRecvFrame(venc);
}

void DestroyVencChannel(VENC_CHN venc) {
    StopRecvFrame(venc);
    (void)HI_MPI_VENC_DestroyChn(venc);
}

void RequestIdrFrame(int32_t venc_channel, VideoCodec codec) {
    if (!IsIdrCodec(codec)) {
        return;
    }
    (void)CheckMpiCall("HI_MPI_VENC_RequestIDR",
                       HI_MPI_VENC_RequestIDR(
                           static_cast<VENC_CHN>(venc_channel), HI_TRUE));
}

bool CloseReEncode(VENC_CHN venc, VENC_RC_MODE_E rc_mode) {
    VENC_RC_PARAM_S rc_param{};
    if (!CheckMpiCall("HI_MPI_VENC_GetRcParam",
                      HI_MPI_VENC_GetRcParam(venc, &rc_param))) {
        return false;
    }

    switch (rc_mode) {
        case VENC_RC_MODE_H264CBR:
            rc_param.stParamH264Cbr.s32MaxReEncodeTimes = 0;
            break;
        case VENC_RC_MODE_H264VBR:
            rc_param.stParamH264Vbr.s32MaxReEncodeTimes = 0;
            break;
        case VENC_RC_MODE_H265CBR:
            rc_param.stParamH265Cbr.s32MaxReEncodeTimes = 0;
            break;
        case VENC_RC_MODE_H265VBR:
            rc_param.stParamH265Vbr.s32MaxReEncodeTimes = 0;
            break;
        default:
            return true;
    }

    return CheckMpiCall("HI_MPI_VENC_SetRcParam",
                        HI_MPI_VENC_SetRcParam(venc, &rc_param));
}

bool BindVpssToVenc(int32_t vpss_group, int32_t vpss_channel,
                    int32_t venc_channel) {
    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = vpss_group;
    src.s32ChnId = vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = venc_channel;

    return CheckMpiCall("HI_MPI_SYS_Bind(VPSS-VENC)",
                        HI_MPI_SYS_Bind(&src, &dst));
}

void UnbindVpssFromVenc(int32_t vpss_group, int32_t vpss_channel,
                        int32_t venc_channel) {
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

// ─── Configure a single VENC channel ───────────────────────────
bool ConfigureVencChannel(int32_t chn, const VideoStreamConfig& stream) {
    if (!ValidateVencStreamConfig(chn, stream)) {
        return false;
    }

    VENC_CHN venc = static_cast<VENC_CHN>(chn);

    VENC_CHN_ATTR_S attr{};
    attr.stVencAttr.enType = PayloadFromCodec(stream.codec);
    attr.stVencAttr.u32MaxPicWidth = stream.size.width;
    attr.stVencAttr.u32MaxPicHeight = stream.size.height;
    attr.stVencAttr.u32PicWidth = stream.size.width;
    attr.stVencAttr.u32PicHeight = stream.size.height;
    attr.stVencAttr.u32BufSize = stream.size.width * stream.size.height * 2;
    attr.stVencAttr.bByFrame = HI_TRUE;
    attr.stVencAttr.u32Profile = 0;  // default profile
    attr.stGopAttr = GopAttrFromConfig(stream.gop_mode, stream.gop);
    const uint32_t stat_time = StatTimeFromConfig(attr.stGopAttr, stream.gop);

    // RC parameters
    attr.stRcAttr.enRcMode = RcModeFromConfig(stream.codec, stream.rc_mode);
    if (stream.codec == VideoCodec::kH264) {
        attr.stVencAttr.stAttrH264e.bRcnRefShareBuf = HI_FALSE;
        if (stream.rc_mode == RateControlMode::kCbr) {
            attr.stRcAttr.stH264Cbr.u32BitRate = stream.bitrate_kbps;
            attr.stRcAttr.stH264Cbr.u32Gop = stream.gop;
            attr.stRcAttr.stH264Cbr.u32StatTime = stat_time;
            attr.stRcAttr.stH264Cbr.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stH264Cbr.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
        } else if (stream.rc_mode == RateControlMode::kVbr) {
            attr.stRcAttr.stH264Vbr.u32MaxBitRate = stream.bitrate_kbps;
            attr.stRcAttr.stH264Vbr.u32Gop = stream.gop;
            attr.stRcAttr.stH264Vbr.u32StatTime = stat_time;
            attr.stRcAttr.stH264Vbr.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stH264Vbr.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
        } else {
            attr.stRcAttr.stH264FixQp.u32Gop = stream.gop;
            attr.stRcAttr.stH264FixQp.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stH264FixQp.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
            attr.stRcAttr.stH264FixQp.u32IQp = kDefaultFixQpI;
            attr.stRcAttr.stH264FixQp.u32PQp = kDefaultFixQpP;
            attr.stRcAttr.stH264FixQp.u32BQp = kDefaultFixQpB;
        }
    } else if (stream.codec == VideoCodec::kH265) {
        attr.stVencAttr.stAttrH265e.bRcnRefShareBuf = HI_FALSE;
        if (stream.rc_mode == RateControlMode::kCbr) {
            attr.stRcAttr.stH265Cbr.u32BitRate = stream.bitrate_kbps;
            attr.stRcAttr.stH265Cbr.u32Gop = stream.gop;
            attr.stRcAttr.stH265Cbr.u32StatTime = stat_time;
            attr.stRcAttr.stH265Cbr.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stH265Cbr.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
        } else if (stream.rc_mode == RateControlMode::kVbr) {
            attr.stRcAttr.stH265Vbr.u32MaxBitRate = stream.bitrate_kbps;
            attr.stRcAttr.stH265Vbr.u32Gop = stream.gop;
            attr.stRcAttr.stH265Vbr.u32StatTime = stat_time;
            attr.stRcAttr.stH265Vbr.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stH265Vbr.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
        } else {
            attr.stRcAttr.stH265FixQp.u32Gop = stream.gop;
            attr.stRcAttr.stH265FixQp.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stH265FixQp.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
            attr.stRcAttr.stH265FixQp.u32IQp = kDefaultFixQpI;
            attr.stRcAttr.stH265FixQp.u32PQp = kDefaultFixQpP;
            attr.stRcAttr.stH265FixQp.u32BQp = kDefaultFixQpB;
        }
    } else if (stream.codec == VideoCodec::kMjpeg) {
        if (stream.rc_mode == RateControlMode::kCbr) {
            attr.stRcAttr.stMjpegCbr.u32BitRate = stream.bitrate_kbps;
            attr.stRcAttr.stMjpegCbr.u32StatTime = stat_time;
            attr.stRcAttr.stMjpegCbr.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stMjpegCbr.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
        } else if (stream.rc_mode == RateControlMode::kVbr) {
            attr.stRcAttr.stMjpegVbr.u32MaxBitRate = stream.bitrate_kbps;
            attr.stRcAttr.stMjpegVbr.u32StatTime = stat_time;
            attr.stRcAttr.stMjpegVbr.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stMjpegVbr.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
        } else {
            attr.stRcAttr.stMjpegFixQp.u32SrcFrameRate = stream.frame_rate.source_fps;
            attr.stRcAttr.stMjpegFixQp.fr32DstFrameRate =
                static_cast<HI_FR32>(stream.frame_rate.target_fps);
            attr.stRcAttr.stMjpegFixQp.u32Qfactor = kDefaultMjpegQfactor;
        }
    }

    if (stream.codec == VideoCodec::kMjpeg) {
        attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
        attr.stGopAttr.stNormalP.s32IPQpDelta = 0;
    }

    INFRA_LOG_INFO(
        "hisi_vendor",
        "Create VENC chn=%d codec=%s rc=%s gop_mode=%s size=%ux%u "
        "src_fps=%d dst_fps=%d bitrate=%u gop=%u stat_time=%u buf=%u",
        chn, CodecName(stream.codec), RcModeName(stream.rc_mode),
        GopModeName(stream.gop_mode), stream.size.width, stream.size.height,
        stream.frame_rate.source_fps, stream.frame_rate.target_fps,
        stream.bitrate_kbps, stream.gop, stat_time, attr.stVencAttr.u32BufSize);
    HISI_CHECK(HI_MPI_VENC_CreateChn(venc, &attr));
    if (!CloseReEncode(venc, attr.stRcAttr.enRcMode)) {
        DestroyVencChannel(venc);
        return false;
    }
    return true;
}

struct VencStreamContext {
    int32_t chn = -1;
    VENC_CHN venc = 0;
    int fd = -1;
    StreamId stream_id = StreamId::kMain;
    VideoCodec codec = VideoCodec::kH264;
};

struct VencPayloadInfo {
    uint32_t size = 0;
};

bool InitVencStreamContext(int32_t chn,
                           StreamId stream_id,
                           VideoCodec codec,
                           VencStreamContext* context) {
    if (context == nullptr) {
        return false;
    }
    context->chn = chn;
    context->venc = static_cast<VENC_CHN>(chn);
    context->stream_id = stream_id;
    context->codec = codec;
    context->fd = HI_MPI_VENC_GetFd(context->venc);
    if (context->fd < 0) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VENC_GetFd failed for channel %d", chn);
        return false;
    }
    if (context->fd >= FD_SETSIZE) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "VENC fd %d exceeds FD_SETSIZE", context->fd);
        return false;
    }
    return true;
}

bool QueryVencStreamStatus(const VencStreamContext& context,
                           VENC_CHN_STATUS_S* status) {
    if (status == nullptr) {
        return false;
    }
    *status = VENC_CHN_STATUS_S{};
    HI_S32 s32_ret = HI_MPI_VENC_QueryStatus(context.venc, status);
    if (s32_ret != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VENC_QueryStatus chn %d failed: 0x%08x",
                        context.chn, s32_ret);
        return false;
    }
    return true;
}

bool GetVencStream(const VencStreamContext& context,
                   const VENC_CHN_STATUS_S& status,
                   VENC_PACK_S** packs,
                   VENC_STREAM_S* stream) {
    if (packs == nullptr || stream == nullptr || status.u32CurPacks == 0) {
        return false;
    }

    *packs = static_cast<VENC_PACK_S*>(
        std::calloc(status.u32CurPacks, sizeof(VENC_PACK_S)));
    if (*packs == nullptr) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "calloc VENC packs chn %d packs=%u failed",
                        context.chn, status.u32CurPacks);
        return false;
    }

    *stream = VENC_STREAM_S{};
    stream->pstPack = *packs;
    stream->u32PackCount = status.u32CurPacks;
    const HI_S32 s32_ret = HI_MPI_VENC_GetStream(context.venc, stream, 0);
    if (s32_ret != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VENC_GetStream chn %d failed: 0x%08x",
                        context.chn, s32_ret);
        std::free(*packs);
        *packs = nullptr;
        return false;
    }
    if (stream->u32PackCount > status.u32CurPacks) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "invalid VENC pack count chn=%d seq=%u packs=%u allocated=%u",
            context.chn, stream->u32Seq, stream->u32PackCount,
            status.u32CurPacks);
        (void)HI_MPI_VENC_ReleaseStream(context.venc, stream);
        std::free(*packs);
        *packs = nullptr;
        return false;
    }
    return true;
}

VENC_STREAM_BUF_INFO_S GetVencStreamBufferInfo(VENC_CHN venc) {
    VENC_STREAM_BUF_INFO_S stream_buffer{};
    if (HI_MPI_VENC_GetStreamBufInfo(venc, &stream_buffer) !=
        HI_SUCCESS) {
        stream_buffer = VENC_STREAM_BUF_INFO_S{};
    }
    return stream_buffer;
}

bool MeasureVencPayload(const VencStreamContext& context,
                        const VENC_STREAM_S& stream,
                        const VENC_STREAM_BUF_INFO_S& stream_buffer,
                        VencPayloadInfo* payload) {
    if (payload == nullptr) {
        return false;
    }
    *payload = VencPayloadInfo{};
    bool valid_stream = stream.u32PackCount > 0 && stream.pstPack != nullptr;
    for (uint32_t i = 0; valid_stream && i < stream.u32PackCount; ++i) {
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(stream.pstPack[i], stream_buffer,
                                         &packet_data)) {
            valid_stream = false;
            break;
        }
        if (payload->size > UINT32_MAX - packet_data.size) {
            valid_stream = false;
            break;
        }
        payload->size += packet_data.size;
    }
    if (!valid_stream || payload->size == 0) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "invalid VENC stream chn=%d seq=%u packs=%u size=%u",
                        context.chn, stream.u32Seq, stream.u32PackCount,
                        payload->size);
        return false;
    }
    return true;
}

VideoBuffer* CopyVencPayload(const VencStreamContext& context,
                             const VENC_STREAM_S& stream,
                             const VENC_STREAM_BUF_INFO_S& stream_buffer,
                             uint32_t payload_size) {
    VideoBuffer* buffer = VideoBufferAlloc(payload_size);
    if (buffer == nullptr) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "alloc VENC payload chn=%d seq=%u size=%u failed",
                        context.chn, stream.u32Seq, payload_size);
        return nullptr;
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        const VENC_PACK_S& pack = stream.pstPack[i];
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(pack, stream_buffer, &packet_data)) {
            INFRA_LOG_ERROR("hisi_vendor",
                            "copy VENC stream invalid pack chn=%d seq=%u "
                            "pack=%u len=%u offset=%u addr=%p",
                            context.chn, stream.u32Seq, i, pack.u32Len,
                            pack.u32Offset, static_cast<void*>(pack.pu8Addr));
            VideoBufferRelease(buffer);
            return nullptr;
        }
        if (packet_data.size > payload_size - offset) {
            INFRA_LOG_ERROR("hisi_vendor",
                            "copy VENC stream overflow chn=%d seq=%u "
                            "offset=%u len=%u size=%u",
                            context.chn, stream.u32Seq, offset,
                            packet_data.size, payload_size);
            VideoBufferRelease(buffer);
            return nullptr;
        }
        if (packet_data.first.size > 0) {
            std::memcpy(buffer->data + offset, packet_data.first.data,
                        packet_data.first.size);
            offset += packet_data.first.size;
        }
        if (packet_data.second.size > 0) {
            std::memcpy(buffer->data + offset, packet_data.second.data,
                        packet_data.second.size);
            offset += packet_data.second.size;
        }
    }
    if (offset != payload_size || !VideoBufferSetSize(buffer, offset)) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "copy VENC stream chn=%d seq=%u size=%u expect=%u "
                        "failed",
                        context.chn, stream.u32Seq, offset, payload_size);
        VideoBufferRelease(buffer);
        return nullptr;
    }
    return buffer;
}

EncodedFrame BuildEncodedFrame(const VencStreamContext& context,
                               const VENC_STREAM_S& stream,
                               FrameType frame_type,
                               VideoBuffer* buffer) {
    EncodedFrame frame;
    frame.stream_id = context.stream_id;
    frame.codec = context.codec;
    frame.frame_type = frame_type;
    frame.sequence = stream.u32Seq;
    frame.pts_us = stream.pstPack[0].u64PTS;
    frame.dts_us = frame.pts_us;
    frame.buffer = buffer;
    frame.offset = 0;
    frame.size = buffer != nullptr ? buffer->size : 0;
    return frame;
}

void ReleaseVencStream(const VencStreamContext& context,
                       VENC_STREAM_S* stream,
                       VENC_PACK_S* packs) {
    if (stream != nullptr &&
        HI_MPI_VENC_ReleaseStream(context.venc, stream) != HI_SUCCESS) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "HI_MPI_VENC_ReleaseStream chn %d failed",
                        context.chn);
    }
    std::free(packs);
}

void HandleVencStream(VencStreamContext* context,
                      EncodedFrameCallback callback,
                      void* user) {
    if (context == nullptr) {
        return;
    }

    VENC_CHN_STATUS_S status{};
    if (!QueryVencStreamStatus(*context, &status) || status.u32CurPacks == 0) {
        return;
    }

    VENC_PACK_S* packs = nullptr;
    VENC_STREAM_S stream{};
    if (!GetVencStream(*context, status, &packs, &stream)) {
        return;
    }

    const VENC_STREAM_BUF_INFO_S stream_buffer =
        GetVencStreamBufferInfo(context->venc);
    VencPayloadInfo payload;
    if (!MeasureVencPayload(*context, stream, stream_buffer, &payload)) {
        ReleaseVencStream(*context, &stream, packs);
        return;
    }

    const FrameType frame_type = FrameTypeFromStream(stream, context->codec);
    VideoBuffer* buffer =
        CopyVencPayload(*context, stream, stream_buffer, payload.size);
    if (buffer == nullptr) {
        ReleaseVencStream(*context, &stream, packs);
        return;
    }
    EncodedFrame frame = BuildEncodedFrame(*context, stream, frame_type, buffer);
    ReleaseVencStream(*context, &stream, packs);

    if (callback != nullptr) {
        callback(frame, user);
    }
}

// Match the HiSilicon sample flow: one reader thread selects all VENC fds.
void VencStreamLoop(MediaPipelineConfig config,
                    EncodedFrameCallback callback,
                    void* user,
                    std::atomic<bool>* running) {
    VencStreamContext streams[2];
    uint32_t stream_count = 0;
    if (!InitVencStreamContext(config.venc_channel, StreamId::kMain,
                               config.main_stream.codec,
                               &streams[stream_count])) {
        return;
    }
    ++stream_count;

    if (config.sub_stream.enabled) {
        if (!InitVencStreamContext(config.sub_venc_channel, StreamId::kSub,
                                   config.sub_stream.codec,
                                   &streams[stream_count])) {
            return;
        }
        ++stream_count;
    }

    while (running->load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = -1;
        for (uint32_t i = 0; i < stream_count; ++i) {
            FD_SET(streams[i].fd, &read_fds);
            if (streams[i].fd > max_fd) {
                max_fd = streams[i].fd;
            }
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        const int ret = select(max_fd + 1, &read_fds, nullptr, nullptr,
                               &timeout);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            INFRA_LOG_ERROR("hisi_vendor", "select on VENC failed: %s",
                            strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;
        }

        for (uint32_t i = 0; i < stream_count; ++i) {
            if (FD_ISSET(streams[i].fd, &read_fds)) {
                HandleVencStream(&streams[i], callback, user);
            }
        }
    }
}

}  // anonymous namespace

// ====================================================================
// StartVenc / StopVenc
// ====================================================================
bool MppHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (impl_->venc_started_) return true;

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

    // Main stream
    if (!ConfigureVencChannel(config.venc_channel, config.main_stream)) {
        return false;
    }

    // Sub stream (if enabled)
    if (config.sub_stream.enabled) {
        if (!ConfigureVencChannel(config.sub_venc_channel, config.sub_stream)) {
            DestroyVencChannel(static_cast<VENC_CHN>(config.venc_channel));
            return false;
        }
    }

    impl_->venc_started_ = true;
    return true;
}

void MppHisiSdk::StopVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (!impl_->venc_started_) return;

    VENC_CHN main_venc = static_cast<VENC_CHN>(config.venc_channel);
    DestroyVencChannel(main_venc);

    if (config.sub_stream.enabled) {
        VENC_CHN sub_venc = static_cast<VENC_CHN>(config.sub_venc_channel);
        DestroyVencChannel(sub_venc);
    }

    impl_->venc_started_ = false;
}

// ====================================================================
// Bind VPSS → VENC
// ====================================================================
bool MppHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (impl_->vpss_bound_venc_) return true;

    // Main stream: VPSS CHN → VENC
    if (!BindVpssToVenc(config.vpss_group, config.vpss_channel,
                        config.venc_channel)) {
        return false;
    }

    // Sub stream (if enabled)
    if (config.sub_stream.enabled) {
        if (!BindVpssToVenc(config.vpss_group, config.sub_vpss_channel,
                            config.sub_venc_channel)) {
            UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                               config.venc_channel);
            return false;
        }
    }

    if (!StartRecvFrame(static_cast<VENC_CHN>(config.venc_channel))) {
        if (config.sub_stream.enabled) {
            UnbindVpssFromVenc(config.vpss_group, config.sub_vpss_channel,
                               config.sub_venc_channel);
        }
        UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                           config.venc_channel);
        return false;
    }

    if (config.sub_stream.enabled &&
        !StartRecvFrame(static_cast<VENC_CHN>(config.sub_venc_channel))) {
        StopRecvFrame(static_cast<VENC_CHN>(config.venc_channel));
        UnbindVpssFromVenc(config.vpss_group, config.sub_vpss_channel,
                           config.sub_venc_channel);
        UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                           config.venc_channel);
        return false;
    }

    RequestIdrFrame(config.venc_channel, config.main_stream.codec);
    if (config.sub_stream.enabled) {
        RequestIdrFrame(config.sub_venc_channel, config.sub_stream.codec);
    }

    impl_->vpss_bound_venc_ = true;
    return true;
}

void MppHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (!impl_->vpss_bound_venc_) return;

    if (impl_->venc_started_) {
        if (config.sub_stream.enabled) {
            StopRecvFrame(static_cast<VENC_CHN>(config.sub_venc_channel));
        }
        StopRecvFrame(static_cast<VENC_CHN>(config.venc_channel));
    }

    // Main stream unbind
    UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                       config.venc_channel);

    if (config.sub_stream.enabled) {
        UnbindVpssFromVenc(config.vpss_group, config.sub_vpss_channel,
                           config.sub_venc_channel);
    }

    impl_->vpss_bound_venc_ = false;
}

// ====================================================================
// StartVencStream / StopVencStream
// ====================================================================
bool MppHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                 EncodedFrameCallback callback,
                                 void* user) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (impl_->stream_started_) return true;

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

    impl_->frame_callback_ = callback;
    impl_->frame_callback_user_ = user;
    impl_->stream_running_.store(true);

    // One stream reader thread monitors all enabled VENC channels.
    impl_->stream_thread_ = std::thread(
        VencStreamLoop, config, callback, user, &impl_->stream_running_);

    impl_->stream_started_ = true;
    return true;
}

void MppHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    // Stop uses the same configured channel set as StartVencStream.
    (void)config;

    if (!impl_->stream_started_ && !impl_->stream_thread_.joinable()) {
        return;
    }

    impl_->stream_running_.store(false);

    if (impl_->stream_thread_.joinable()) {
        impl_->stream_thread_.join();
    }

    impl_->frame_callback_ = nullptr;
    impl_->frame_callback_user_ = nullptr;
    impl_->stream_started_ = false;
}

// ====================================================================
// RequestIdr
// ====================================================================
bool MppHisiSdk::RequestIdr(int32_t venc_channel) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (venc_channel < 0) return false;

    return internal::HiOk(HI_MPI_VENC_RequestIDR(
        static_cast<VENC_CHN>(venc_channel), HI_TRUE));
}

}  // namespace hisisdk
}  // namespace live_stream
