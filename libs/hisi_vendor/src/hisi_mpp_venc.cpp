#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <poll.h>
#include <thread>
#include <utility>
#include <vector>

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
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
constexpr uint32_t kMaxVencPacksPerFrame = 256;
constexpr uint32_t kPayloadPreviewBytes = 16;

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

const char* FrameTypeName(FrameType frame_type) {
    switch (frame_type) {
        case FrameType::kIdr:
            return "idr";
        case FrameType::kI:
            return "i";
        case FrameType::kP:
            return "p";
        case FrameType::kB:
            return "b";
        case FrameType::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

int PackTypeValue(const VENC_PACK_S& pack, VideoCodec codec) {
    if (codec == VideoCodec::kH264) {
        return static_cast<int>(pack.DataType.enH264EType);
    }
    if (codec == VideoCodec::kH265) {
        return static_cast<int>(pack.DataType.enH265EType);
    }
    if (codec == VideoCodec::kJpeg || codec == VideoCodec::kMjpeg) {
        return static_cast<int>(pack.DataType.enJPEGEType);
    }
    return -1;
}

void FormatHexPreview(const uint8_t* data, uint32_t size, char* output,
                      uint32_t output_size) {
    if (output == nullptr || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (data == nullptr || size == 0) {
        return;
    }

    uint32_t written = 0;
    const uint32_t preview_size =
        size < kPayloadPreviewBytes ? size : kPayloadPreviewBytes;
    for (uint32_t i = 0; i < preview_size && written < output_size; ++i) {
        const int ret = std::snprintf(
            output + written, output_size - written, "%s%02x",
            i == 0 ? "" : " ", static_cast<unsigned>(data[i]));
        if (ret <= 0) {
            return;
        }
        const uint32_t used = static_cast<uint32_t>(ret);
        if (used >= output_size - written) {
            output[output_size - 1] = '\0';
            return;
        }
        written += used;
    }
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

uint32_t VencPackDataLen(const VENC_PACK_S& pack) {
    if (pack.pu8Addr == nullptr || pack.u32Len <= pack.u32Offset) {
        return 0;
    }
    return pack.u32Len - pack.u32Offset;
}

bool HasValidVencStreamPacks(const VENC_STREAM_S& stream) {
    return stream.pstPack != nullptr && stream.u32PackCount > 0 &&
           stream.u32PackCount <= kMaxVencPacksPerFrame;
}

FrameType FrameTypeFromStream(const VENC_STREAM_S& stream, VideoCodec codec) {
    if (codec == VideoCodec::kJpeg || codec == VideoCodec::kMjpeg) {
        return FrameType::kJpeg;
    }
    if (!HasValidVencStreamPacks(stream)) {
        return FrameType::kP;
    }

    bool has_i_slice = false;
    bool has_b_slice = false;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        const VENC_PACK_S& pack = stream.pstPack[i];
        const uint32_t data_num = pack.u32DataNum < 8 ? pack.u32DataNum : 8;
        if (codec == VideoCodec::kH264) {
            if (pack.DataType.enH264EType == H264E_NALU_IDRSLICE) {
                return FrameType::kIdr;
            }
            if (pack.DataType.enH264EType == H264E_NALU_ISLICE) {
                has_i_slice = true;
            } else if (pack.DataType.enH264EType == H264E_NALU_BSLICE) {
                has_b_slice = true;
            }
            for (uint32_t j = 0; j < data_num; ++j) {
                const H264E_NALU_TYPE_E type =
                    pack.stPackInfo[j].u32PackType.enH264EType;
                if (type == H264E_NALU_IDRSLICE) {
                    return FrameType::kIdr;
                }
                if (type == H264E_NALU_ISLICE) {
                    has_i_slice = true;
                } else if (type == H264E_NALU_BSLICE) {
                    has_b_slice = true;
                }
            }
        } else if (codec == VideoCodec::kH265) {
            if (pack.DataType.enH265EType == H265E_NALU_IDRSLICE) {
                return FrameType::kIdr;
            }
            if (pack.DataType.enH265EType == H265E_NALU_ISLICE) {
                has_i_slice = true;
            } else if (pack.DataType.enH265EType == H265E_NALU_BSLICE) {
                has_b_slice = true;
            }
            for (uint32_t j = 0; j < data_num; ++j) {
                const H265E_NALU_TYPE_E type =
                    pack.stPackInfo[j].u32PackType.enH265EType;
                if (type == H265E_NALU_IDRSLICE) {
                    return FrameType::kIdr;
                }
                if (type == H265E_NALU_ISLICE) {
                    has_i_slice = true;
                } else if (type == H265E_NALU_BSLICE) {
                    has_b_slice = true;
                }
            }
        }
    }

    if (has_i_slice) {
        return FrameType::kI;
    }
    if (has_b_slice) {
        return FrameType::kB;
    }
    return FrameType::kP;
}

bool CopyVencStreamPayloads(const VENC_STREAM_S& stream,
                            std::shared_ptr<IMediaBuffer>* buffer,
                            uint32_t* size) {
    if (buffer == nullptr || size == nullptr ||
        !HasValidVencStreamPacks(stream)) {
        return false;
    }

    uint64_t total_len = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        total_len += VencPackDataLen(stream.pstPack[i]);
    }
    if (total_len == 0 ||
        total_len > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    std::shared_ptr<IMediaBuffer> payload =
        CreateMediaBuffer(static_cast<uint32_t>(total_len));
    if (!payload) {
        return false;
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        const VENC_PACK_S& pack = stream.pstPack[i];
        const uint32_t data_len = VencPackDataLen(pack);
        if (data_len == 0) {
            continue;
        }
        const uint8_t* data =
            static_cast<const uint8_t*>(pack.pu8Addr) + pack.u32Offset;
        std::memcpy(payload->MutableData() + offset, data, data_len);
        offset += data_len;
    }
    if (offset == 0 || !payload->SetSize(offset)) {
        return false;
    }

    *buffer = std::move(payload);
    *size = offset;
    return true;
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

// ─── Stream reader thread ──────────────────────────────────────
void VencStreamLoop(int32_t chn, StreamId stream_id, VideoCodec codec,
                    EncodedFrameCallback callback, void* user,
                    std::atomic<bool>* running) {
    VENC_CHN venc = static_cast<VENC_CHN>(chn);
    int fd = HI_MPI_VENC_GetFd(venc);
    if (fd < 0) {
        INFRA_LOG_ERROR("hisi_vendor", "HI_MPI_VENC_GetFd failed for channel %d", chn);
        return;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN | POLLERR;
    bool first_frame_logged = false;

    while (running->load()) {
        int ret = poll(&pfd, 1, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            INFRA_LOG_ERROR("hisi_vendor", "poll on VENC %d failed: %s", chn, strerror(errno));
            break;
        }
        if (ret == 0) continue;  // timeout

        if (pfd.revents & POLLERR) {
            INFRA_LOG_ERROR("hisi_vendor", "POLLERR on VENC fd %d", fd);
            break;
        }

        VENC_CHN_STATUS_S status{};
        HI_S32 s32_ret = HI_MPI_VENC_QueryStatus(venc, &status);
        if (s32_ret != HI_SUCCESS) {
            INFRA_LOG_ERROR("hisi_vendor",
                            "HI_MPI_VENC_QueryStatus chn %d failed: 0x%08x", chn,
                            s32_ret);
            continue;
        }
        if (status.u32CurPacks == 0 || status.u32LeftStreamFrames == 0) {
            continue;
        }
        if (status.u32CurPacks > kMaxVencPacksPerFrame) {
            INFRA_LOG_ERROR(
                "hisi_vendor",
                "VENC chn %d returned too many packs: cur=%u left_frames=%u",
                chn, status.u32CurPacks, status.u32LeftStreamFrames);
            continue;
        }

        std::vector<VENC_PACK_S> packs(status.u32CurPacks);
        VENC_STREAM_S stream{};
        stream.pstPack = packs.data();
        stream.u32PackCount = status.u32CurPacks;
        s32_ret = HI_MPI_VENC_GetStream(venc, &stream, 0);
        if (s32_ret != HI_SUCCESS) {
            INFRA_LOG_ERROR("hisi_vendor",
                            "HI_MPI_VENC_GetStream chn %d failed: 0x%08x", chn,
                            s32_ret);
            continue;
        }

        std::shared_ptr<IMediaBuffer> buffer;
        uint32_t payload_size = 0;
        const bool copied =
            CopyVencStreamPayloads(stream, &buffer, &payload_size);
        if (!copied) {
            INFRA_LOG_ERROR(
                "hisi_vendor",
                "invalid VENC stream chn=%d seq=%u packs=%u cur=%u "
                "left_frames=%u",
                chn, stream.u32Seq, stream.u32PackCount, status.u32CurPacks,
                status.u32LeftStreamFrames);
            HI_MPI_VENC_ReleaseStream(venc, &stream);
            continue;
        }

        EncodedFrame frame;
        frame.stream_id = stream_id;
        frame.codec = codec;
        frame.frame_type = FrameTypeFromStream(stream, codec);
        frame.sequence = stream.u32Seq;
        frame.pts_us = stream.pstPack[0].u64PTS;
        frame.dts_us = frame.pts_us;
        frame.buffer = std::move(buffer);
        frame.offset = 0;
        frame.size = payload_size;

        if (!first_frame_logged) {
            const VENC_PACK_S& first_pack = stream.pstPack[0];
            char payload_preview[kPayloadPreviewBytes * 3] = {};
            FormatHexPreview(frame.PayloadData(),
                             frame.size, payload_preview,
                             static_cast<uint32_t>(sizeof(payload_preview)));
            INFRA_LOG_INFO(
                "hisi_vendor",
                "VENC first frame chn=%d codec=%s seq=%u frame_seq=%llu "
                "packs=%u size=%u pts=%lld dts=%lld frame_type=%s "
                "pack_type=%d first_len=%u first_offset=%u first_data=%u "
                "data_num=%u first_addr=%p head=%s",
                chn, CodecName(codec), stream.u32Seq,
                static_cast<unsigned long long>(frame.sequence),
                stream.u32PackCount, frame.size,
                static_cast<long long>(frame.pts_us),
                static_cast<long long>(frame.dts_us),
                FrameTypeName(frame.frame_type),
                PackTypeValue(first_pack, codec), first_pack.u32Len,
                first_pack.u32Offset, VencPackDataLen(first_pack),
                first_pack.u32DataNum, first_pack.pu8Addr, payload_preview);
            first_frame_logged = true;
        }

        if (HI_MPI_VENC_ReleaseStream(venc, &stream) != HI_SUCCESS) {
            INFRA_LOG_ERROR("hisi_vendor",
                            "HI_MPI_VENC_ReleaseStream chn %d failed", chn);
        }

        if (callback) {
            callback(frame, user);
        }
    }
}

}  // anonymous namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

// ====================================================================
// StartVenc / StopVenc
// ====================================================================
bool MppHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    if (impl_->venc_started_) return true;

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
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

#else
    (void)config;
    impl_->venc_started_ = true;
    return true;
#endif
}

void MppHisiSdk::StopVenc(const MediaPipelineConfig& config) {
    if (!impl_->venc_started_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    VENC_CHN main_venc = static_cast<VENC_CHN>(config.venc_channel);
    DestroyVencChannel(main_venc);

    if (config.sub_stream.enabled) {
        VENC_CHN sub_venc = static_cast<VENC_CHN>(config.sub_venc_channel);
        DestroyVencChannel(sub_venc);
    }
#else
    (void)config;
#endif

    impl_->venc_started_ = false;
}

// ====================================================================
// Bind VPSS → VENC
// ====================================================================
bool MppHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    if (impl_->vpss_bound_venc_) return true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
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

#else
    (void)config;
    impl_->vpss_bound_venc_ = true;
    return true;
#endif
}

void MppHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
    if (!impl_->vpss_bound_venc_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
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
#else
    (void)config;
#endif

    impl_->vpss_bound_venc_ = false;
}

// ====================================================================
// StartVencStream / StopVencStream
// ====================================================================
bool MppHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                 EncodedFrameCallback callback,
                                 void* user) {
    if (impl_->stream_started_) return true;

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    impl_->frame_callback_ = callback;
    impl_->frame_callback_user_ = user;
    impl_->stream_running_.store(true);

    // Main stream thread
    impl_->main_stream_thread_ = std::thread(
        VencStreamLoop, config.venc_channel, StreamId::kMain,
        config.main_stream.codec, callback, user, &impl_->stream_running_);

    // Sub stream thread (if enabled)
    if (config.sub_stream.enabled) {
        impl_->sub_stream_thread_ = std::thread(
            VencStreamLoop, config.sub_venc_channel, StreamId::kSub,
            config.sub_stream.codec, callback, user, &impl_->stream_running_);
    }

    impl_->stream_started_ = true;
    return true;

#else
    (void)config;
    (void)callback;
    (void)user;
    impl_->stream_started_ = true;
    return true;
#endif
}

void MppHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    // Stop uses the same configured channel set as StartVencStream.
    (void)config;
#else
    (void)config;
#endif

    if (!impl_->stream_started_ && !impl_->main_stream_thread_.joinable() &&
        !impl_->sub_stream_thread_.joinable()) {
        return;
    }

    impl_->stream_running_.store(false);

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    if (impl_->main_stream_thread_.joinable()) {
        impl_->main_stream_thread_.join();
    }
    if (impl_->sub_stream_thread_.joinable()) {
        impl_->sub_stream_thread_.join();
    }
#endif

    impl_->frame_callback_ = nullptr;
    impl_->frame_callback_user_ = nullptr;
    impl_->stream_started_ = false;
}

// ====================================================================
// RequestIdr
// ====================================================================
bool MppHisiSdk::RequestIdr(int32_t venc_channel) {
    if (venc_channel < 0) return false;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    return internal::HiOk(HI_MPI_VENC_RequestIDR(
        static_cast<VENC_CHN>(venc_channel), HI_TRUE));
#else
    (void)venc_channel;
    return true;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
