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
#include <utility>

namespace live_stream {
namespace hisisdk {
namespace {

constexpr uint32_t kDefaultJpegQuality = 50;
constexpr uint32_t kJpegMaxStreamCount = 200;
constexpr uint32_t kJpegIpQpDelta = 2;

struct VpssMappedFrame {
    VPSS_GRP group = 0;
    VPSS_CHN channel = 0;
    VIDEO_FRAME_INFO_S frame_info;
};

struct JpegCaptureContext {
    VPSS_GRP vpss_group = 0;
    VPSS_CHN vpss_channel = 0;
    VENC_CHN jpeg_channel = 0;
    HI_S32 timeout = 0;
    VIDEO_FRAME_INFO_S frame_info;
    bool has_frame = false;
    bool has_channel = false;
    bool receiving = false;
};

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

MppYuvFrameInfo MakeMppYuvFrameInfo(const VIDEO_FRAME_INFO_S& frame_info) {
    const VIDEO_FRAME_S& frame = frame_info.stVFrame;
    MppYuvFrameInfo info;
    info.valid = true;
    for (uint32_t i = 0; i < 3; ++i) {
        info.phy_addr[i] = frame.u64PhyAddr[i];
        info.vir_addr[i] = frame.u64VirAddr[i];
        info.header_phy_addr[i] = frame.u64HeaderPhyAddr[i];
        info.header_vir_addr[i] = frame.u64HeaderVirAddr[i];
        info.ext_phy_addr[i] = frame.u64ExtPhyAddr[i];
        info.ext_vir_addr[i] = frame.u64ExtVirAddr[i];
        info.stride[i] = frame.u32Stride[i];
        info.header_stride[i] = frame.u32HeaderStride[i];
        info.ext_stride[i] = frame.u32ExtStride[i];
    }
    if (info.stride[1] == 0) {
        info.stride[1] = info.stride[0];
    }
    if (info.phy_addr[1] == 0 && info.phy_addr[0] != 0 &&
        info.stride[0] != 0) {
        info.phy_addr[1] = info.phy_addr[0] +
                           static_cast<uint64_t>(info.stride[0]) *
                               frame.u32Height;
    }
    if (info.vir_addr[1] == 0 && info.vir_addr[0] != 0 &&
        info.stride[0] != 0) {
        info.vir_addr[1] = info.vir_addr[0] +
                           static_cast<uint64_t>(info.stride[0]) *
                               frame.u32Height;
    }
    info.width = frame.u32Width;
    info.height = frame.u32Height;
    info.pool_id = frame_info.u32PoolId;
    info.max_luminance = frame.u32MaxLuminance;
    info.min_luminance = frame.u32MinLuminance;
    info.time_ref = frame.u32TimeRef;
    info.frame_flag = frame.u32FrameFlag;
    info.module_id = static_cast<int32_t>(frame_info.enModId);
    info.field = static_cast<int32_t>(frame.enField);
    info.pixel_format = static_cast<int32_t>(frame.enPixelFormat);
    info.video_format = static_cast<int32_t>(frame.enVideoFormat);
    info.compress_mode = static_cast<int32_t>(frame.enCompressMode);
    info.dynamic_range = static_cast<int32_t>(frame.enDynamicRange);
    info.color_gamut = static_cast<int32_t>(frame.enColorGamut);
    info.offset_top = frame.s16OffsetTop;
    info.offset_bottom = frame.s16OffsetBottom;
    info.offset_left = frame.s16OffsetLeft;
    info.offset_right = frame.s16OffsetRight;
    return info;
}

void ReleaseVpssMappedFrame(uint8_t* data, uint32_t capacity, void* user) {
    VpssMappedFrame* mapped_frame = static_cast<VpssMappedFrame*>(user);
    if (mapped_frame == nullptr) {
        return;
    }
    if (data != nullptr && capacity > 0) {
        const HI_S32 munmap_status = HI_MPI_SYS_Munmap(data, capacity);
        if (munmap_status != HI_SUCCESS) {
            Error(
                "hisi_vendor",
                "CaptureYuvFrame: HI_MPI_SYS_Munmap failed: 0x%08x",
                munmap_status);
        }
    }
    const HI_S32 release_status = HI_MPI_VPSS_ReleaseChnFrame(
        mapped_frame->group, mapped_frame->channel, &mapped_frame->frame_info);
    if (release_status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "CaptureYuvFrame: HI_MPI_VPSS_ReleaseChnFrame failed: 0x%08x",
            release_status);
    }
    std::free(mapped_frame);
}

uint32_t JpegQualityFromConfig(uint32_t quality) {
    if (quality == 0) {
        return kDefaultJpegQuality;
    }
    uint32_t qfactor = quality * 99 / 100;
    if (qfactor == 0) {
        qfactor = 1;
    }
    return infra::Clamp<uint32_t>(qfactor, 1U, 99U);
}

bool EnsureVpssFrameDepth(VPSS_GRP group, VPSS_CHN channel) {
    if (channel >= VPSS_MAX_PHY_CHN_NUM) {
        VPSS_EXT_CHN_ATTR_S attr{};
        if (!MpiOk("HI_MPI_VPSS_GetExtChnAttr",
                          HI_MPI_VPSS_GetExtChnAttr(group, channel, &attr))) {
            return false;
        }
        if (attr.u32Depth == 0) {
            attr.u32Depth = 2;
            return MpiOk("HI_MPI_VPSS_SetExtChnAttr",
                                HI_MPI_VPSS_SetExtChnAttr(group, channel,
                                                          &attr));
        }
        return true;
    }

    VPSS_CHN_ATTR_S attr{};
    if (!MpiOk("HI_MPI_VPSS_GetChnAttr",
                      HI_MPI_VPSS_GetChnAttr(group, channel, &attr))) {
        return false;
    }
    if (attr.u32Depth == 0) {
        attr.u32Depth = 2;
        return MpiOk("HI_MPI_VPSS_SetChnAttr",
                            HI_MPI_VPSS_SetChnAttr(group, channel, &attr));
    }
    return true;
}

void ReleaseCaptureContext(JpegCaptureContext* context) {
    if (context == nullptr) {
        return;
    }
    if (context->receiving) {
        (void)HI_MPI_VENC_StopRecvFrame(context->jpeg_channel);
        context->receiving = false;
    }
    if (context->has_channel) {
        (void)HI_MPI_VENC_DestroyChn(context->jpeg_channel);
        context->has_channel = false;
    }
    if (context->has_frame) {
        (void)MpiOk("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(
                               context->vpss_group, context->vpss_channel,
                               &context->frame_info));
        context->has_frame = false;
    }
}

bool InitJpegCaptureContext(const SnapshotConfig& config,
                            JpegCaptureContext* context) {
    if (context == nullptr) {
        return false;
    }
    *context = JpegCaptureContext{};
    context->vpss_group = static_cast<VPSS_GRP>(config.snap_vpss_group);
    context->vpss_channel = static_cast<VPSS_CHN>(config.snap_vpss_channel);
    context->jpeg_channel = static_cast<VENC_CHN>(config.jpeg_venc_channel);
    context->timeout = static_cast<HI_S32>(config.timeout_ms);

    if (!MpiOk("CaptureJpeg: HI_MPI_VPSS_GetChnFrame",
                      HI_MPI_VPSS_GetChnFrame(
                          context->vpss_group, context->vpss_channel,
                          &context->frame_info, context->timeout))) {
        return false;
    }
    context->has_frame = true;
    const VIDEO_FRAME_S& source_frame = context->frame_info.stVFrame;
    if (source_frame.u32Width == 0 || source_frame.u32Height == 0) {
        return false;
    }
    return true;
}

VENC_CHN_ATTR_S MakeJpegChannelAttr(const SnapshotConfig& config,
                                    const VIDEO_FRAME_S& source_frame) {
    const uint32_t jpeg_buffer_size =
        AlignUp(source_frame.u32Width, 32) *
        AlignUp(source_frame.u32Height, 32) * 2;

    VENC_CHN_ATTR_S attr{};
    attr.stVencAttr.enType = PT_MJPEG;
    attr.stVencAttr.u32MaxPicWidth = source_frame.u32Width;
    attr.stVencAttr.u32MaxPicHeight = source_frame.u32Height;
    attr.stVencAttr.u32PicWidth = source_frame.u32Width;
    attr.stVencAttr.u32PicHeight = source_frame.u32Height;
    attr.stVencAttr.u32BufSize = jpeg_buffer_size;
    attr.stVencAttr.bByFrame = HI_TRUE;
    attr.stVencAttr.u32Profile = 0;
    attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
    attr.stRcAttr.stMjpegFixQp.u32SrcFrameRate = 1;
    attr.stRcAttr.stMjpegFixQp.fr32DstFrameRate = 1;
    attr.stRcAttr.stMjpegFixQp.u32Qfactor =
        JpegQualityFromConfig(config.jpeg_quality);
    attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attr.stGopAttr.stNormalP.s32IPQpDelta = kJpegIpQpDelta;
    return attr;
}

bool CreateJpegChannel(const SnapshotConfig& config,
                       JpegCaptureContext* context) {
    if (context == nullptr || !context->has_frame) {
        return false;
    }
    VENC_CHN_ATTR_S attr =
        MakeJpegChannelAttr(config, context->frame_info.stVFrame);
    if (!MpiOk("CaptureJpeg: HI_MPI_VENC_CreateChn",
                      HI_MPI_VENC_CreateChn(context->jpeg_channel, &attr))) {
        return false;
    }
    context->has_channel = true;

    VENC_CHN_PARAM_S channel_param{};
    channel_param.u32MaxStrmCnt = kJpegMaxStreamCount;
    channel_param.u32PollWakeUpFrmCnt = 1;
    channel_param.stFrameRate.s32SrcFrmRate = 1;
    channel_param.stFrameRate.s32DstFrmRate = 1;
    return MpiOk("CaptureJpeg: HI_MPI_VENC_SetChnParam",
                        HI_MPI_VENC_SetChnParam(context->jpeg_channel,
                                                &channel_param));
}

bool SendJpegFrame(JpegCaptureContext* context) {
    if (context == nullptr || !context->has_channel || !context->has_frame) {
        return false;
    }
    VENC_RECV_PIC_PARAM_S recv_param{};
    recv_param.s32RecvPicNum = -1;
    if (!MpiOk("CaptureJpeg: HI_MPI_VENC_StartRecvFrame",
                      HI_MPI_VENC_StartRecvFrame(context->jpeg_channel,
                                                &recv_param))) {
        return false;
    }
    context->receiving = true;
    if (!MpiOk("CaptureJpeg: HI_MPI_VENC_SendFrame",
                      HI_MPI_VENC_SendFrame(context->jpeg_channel,
                                            &context->frame_info,
                                            context->timeout))) {
        return false;
    }
    if (context->has_frame) {
        (void)MpiOk("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(
                               context->vpss_group, context->vpss_channel,
                               &context->frame_info));
        context->has_frame = false;
    }
    return true;
}

bool WaitJpegStream(VENC_CHN jpeg_channel, uint32_t timeout_ms) {
    const int fd = HI_MPI_VENC_GetFd(jpeg_channel);
    if (fd < 0 || fd >= FD_SETSIZE) {
        Error("hisi_vendor",
                        "CaptureJpeg: HI_MPI_VENC_GetFd failed: %d", fd);
        return false;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    timeval select_timeout{};
    select_timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    select_timeout.tv_usec =
        static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    const int select_ret =
        select(fd + 1, &read_fds, nullptr, nullptr, &select_timeout);
    if (select_ret <= 0 || !FD_ISSET(fd, &read_fds)) {
        if (select_ret < 0) {
            Error("hisi_vendor", "CaptureJpeg: select failed: %s",
                            strerror(errno));
        } else {
            Error("hisi_vendor",
                            "CaptureJpeg: timed out after %u ms",
                            timeout_ms);
        }
        return false;
    }
    return true;
}

bool QueryJpegPacks(VENC_CHN jpeg_channel, VENC_CHN_STATUS_S* status) {
    if (status == nullptr) {
        return false;
    }
    *status = VENC_CHN_STATUS_S{};
    if (!MpiOk("CaptureJpeg: HI_MPI_VENC_QueryStatus",
                      HI_MPI_VENC_QueryStatus(jpeg_channel, status))) {
        return false;
    }
    if (status->u32CurPacks == 0) {
        Error("hisi_vendor", "CaptureJpeg: no JPEG packs available");
        return false;
    }
    return true;
}

bool GetJpegStream(VENC_CHN jpeg_channel, const VENC_CHN_STATUS_S& status,
                   VENC_PACK_S** packs, VENC_STREAM_S* stream) {
    if (packs == nullptr || stream == nullptr) {
        return false;
    }
    *packs = static_cast<VENC_PACK_S*>(
        std::calloc(status.u32CurPacks, sizeof(VENC_PACK_S)));
    if (*packs == nullptr) {
        Error("hisi_vendor",
                        "CaptureJpeg: calloc packs=%u failed",
                        status.u32CurPacks);
        return false;
    }
    *stream = VENC_STREAM_S{};
    stream->pstPack = *packs;
    stream->u32PackCount = status.u32CurPacks;
    if (!MpiOk("CaptureJpeg: HI_MPI_VENC_GetStream",
                      HI_MPI_VENC_GetStream(jpeg_channel, stream, -1))) {
        std::free(*packs);
        *packs = nullptr;
        return false;
    }
    return true;
}

VENC_STREAM_BUF_INFO_S GetJpegStreamBufferInfo(VENC_CHN jpeg_channel) {
    VENC_STREAM_BUF_INFO_S stream_buffer{};
    if (HI_MPI_VENC_GetStreamBufInfo(jpeg_channel, &stream_buffer) !=
        HI_SUCCESS) {
        stream_buffer = VENC_STREAM_BUF_INFO_S{};
    }
    return stream_buffer;
}

bool MeasureJpegPayload(const VENC_STREAM_S& stream,
                        const VENC_CHN_STATUS_S& status,
                        const VENC_STREAM_BUF_INFO_S& stream_buffer,
                        uint32_t* payload_size) {
    if (payload_size == nullptr) {
        return false;
    }
    *payload_size = 0;
    if (stream.u32PackCount == 0 || stream.u32PackCount > status.u32CurPacks ||
        stream.pstPack == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(stream.pstPack[i], stream_buffer,
                                         &packet_data) ||
            *payload_size > UINT32_MAX - packet_data.size) {
            return false;
        }
        *payload_size += packet_data.size;
    }
    return *payload_size > 0;
}

VideoBuffer* CopyJpegPayload(const VENC_STREAM_S& stream,
                             const VENC_STREAM_BUF_INFO_S& stream_buffer,
                             uint32_t payload_size) {
    VideoBuffer* buffer = VideoBufferAlloc(payload_size);
    if (buffer == nullptr) {
        return nullptr;
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        const VENC_PACK_S& pack = stream.pstPack[i];
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(pack, stream_buffer, &packet_data) ||
            packet_data.size > payload_size - offset) {
            VideoBufferUnref(buffer);
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
        VideoBufferUnref(buffer);
        return nullptr;
    }
    return buffer;
}

void ReleaseJpegStream(VENC_CHN jpeg_channel, VENC_STREAM_S* stream,
                       VENC_PACK_S* packs) {
    if (stream != nullptr) {
        const HI_S32 release_status =
            HI_MPI_VENC_ReleaseStream(jpeg_channel, stream);
        if (release_status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "CaptureJpeg: HI_MPI_VENC_ReleaseStream failed: 0x%08x",
            release_status);
        }
    }
    std::free(packs);
}

JpegFrame ReadJpegResult(VENC_CHN jpeg_channel, uint32_t width,
                         uint32_t height, uint32_t timeout_ms) {
    JpegFrame result{};
    result.width = width;
    result.height = height;
    if (!WaitJpegStream(jpeg_channel, timeout_ms)) {
        return result;
    }

    VENC_CHN_STATUS_S status{};
    if (!QueryJpegPacks(jpeg_channel, &status)) {
        return result;
    }
    VENC_PACK_S* packs = nullptr;
    VENC_STREAM_S stream{};
    if (!GetJpegStream(jpeg_channel, status, &packs, &stream)) {
        return result;
    }

    const VENC_STREAM_BUF_INFO_S stream_buffer =
        GetJpegStreamBufferInfo(jpeg_channel);
    uint32_t payload_size = 0;
    VideoBuffer* buffer = nullptr;
    if (MeasureJpegPayload(stream, status, stream_buffer, &payload_size)) {
        buffer = CopyJpegPayload(stream, stream_buffer, payload_size);
    }
    if (buffer != nullptr) {
        result.buffer = buffer;
        result.offset = 0;
        result.size = buffer->size;
        result.pts_us = stream.pstPack[0].u64PTS;
    } else {
        Error("hisi_vendor", "CaptureJpeg: invalid JPEG stream");
    }
    ReleaseJpegStream(jpeg_channel, &stream, packs);
    return result;
}

}  // namespace

JpegFrame MppHisiSdk::CaptureJpeg(const SnapshotConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    JpegFrame result{};
    result.width = config.size.width;
    result.height = config.size.height;

    if (!impl_->system_initialized_) {
        Error("hisi_vendor", "CaptureJpeg: system not initialized");
        return result;
    }
    if (config.jpeg_venc_channel < 0 || config.snap_vpss_group < 0 ||
        config.snap_vpss_channel < 0 || config.size.width == 0 ||
        config.size.height == 0 || config.timeout_ms == 0) {
        return result;
    }

    JpegCaptureContext context;
    if (!InitJpegCaptureContext(config, &context) ||
        !CreateJpegChannel(config, &context) || !SendJpegFrame(&context)) {
        ReleaseCaptureContext(&context);
        return result;
    }
    result = ReadJpegResult(context.jpeg_channel,
                            context.frame_info.stVFrame.u32Width,
                            context.frame_info.stVFrame.u32Height,
                            config.timeout_ms);
    ReleaseCaptureContext(&context);
    return result;
}

YuvFrame MppHisiSdk::CaptureYuvFrame(const MppChannel& vpss_channel,
                                     Size size,
                                     uint32_t timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    YuvFrame result;
    result.width = size.width;
    result.height = size.height;
    if (!impl_->system_initialized_) {
        Error("hisi_vendor", "CaptureYuvFrame: system not initialized");
        return result;
    }
    if (vpss_channel.module != MppModule::kVpss || size.width == 0 ||
        size.height == 0 || timeout_ms == 0) {
        return result;
    }

    VIDEO_FRAME_INFO_S frame_info{};
    const VPSS_GRP group = static_cast<VPSS_GRP>(vpss_channel.device);
    const VPSS_CHN channel = static_cast<VPSS_CHN>(vpss_channel.channel);
    if (!EnsureVpssFrameDepth(group, channel)) {
        return result;
    }

    const HI_S32 timeout = static_cast<HI_S32>(timeout_ms);
    if (!MpiOk("CaptureYuvFrame: HI_MPI_VPSS_GetChnFrame",
                      HI_MPI_VPSS_GetChnFrame(group, channel, &frame_info,
                                              timeout))) {
        return result;
    }

    const VIDEO_FRAME_S& frame = frame_info.stVFrame;
    const uint32_t width = frame.u32Width;
    const uint32_t height = frame.u32Height;
    const uint32_t stride_y = frame.u32Stride[0];
    const uint32_t stride_uv = frame.u32Stride[1] != 0 ? frame.u32Stride[1]
                                                       : stride_y;
    const uint32_t y_size = stride_y * height;
    const uint32_t uv_size = stride_uv * height / 2;
    const uint32_t total_size = y_size + uv_size;
    if (stride_y == 0 || width == 0 || height == 0 ||
        frame.u64PhyAddr[0] == 0 || total_size == 0) {
        Error(
            "hisi_vendor",
            "CaptureYuvFrame: invalid frame grp=%d chn=%d width=%u height=%u "
            "stride_y=%u stride_uv=%u phy0=0x%llx phy1=0x%llx vir=0x%llx "
            "size=%u",
            vpss_channel.device, vpss_channel.channel, width, height,
            stride_y, stride_uv,
            static_cast<unsigned long long>(frame.u64PhyAddr[0]),
            static_cast<unsigned long long>(frame.u64PhyAddr[1]),
            static_cast<unsigned long long>(frame.u64VirAddr[0]),
            total_size);
        (void)HI_MPI_VPSS_ReleaseChnFrame(group, channel, &frame_info);
        return result;
    }
    void* mapped = HI_MPI_SYS_MmapCache(frame.u64PhyAddr[0], total_size);
    if (mapped == nullptr) {
        Error(
            "hisi_vendor",
            "CaptureYuvFrame: mmap skipped grp=%d chn=%d phy=0x%llx "
            "vir=0x%llx size=%u",
            vpss_channel.device, vpss_channel.channel,
            static_cast<unsigned long long>(frame.u64PhyAddr[0]),
            static_cast<unsigned long long>(frame.u64VirAddr[0]), total_size);
        (void)HI_MPI_VPSS_ReleaseChnFrame(group, channel, &frame_info);
        return result;
    }

    VpssMappedFrame* mapped_frame =
        static_cast<VpssMappedFrame*>(std::calloc(1, sizeof(VpssMappedFrame)));
    if (mapped_frame == nullptr) {
        (void)HI_MPI_SYS_Munmap(mapped, total_size);
        (void)HI_MPI_VPSS_ReleaseChnFrame(group, channel, &frame_info);
        return result;
    }
    mapped_frame->group = group;
    mapped_frame->channel = channel;
    mapped_frame->frame_info = frame_info;

    VideoBuffer* buffer = VideoBufferCreateExternal(
        static_cast<uint8_t*>(mapped), total_size, total_size,
        ReleaseVpssMappedFrame, mapped_frame);
    if (buffer == nullptr) {
        ReleaseVpssMappedFrame(static_cast<uint8_t*>(mapped), total_size,
                               mapped_frame);
        return result;
    }
    result.buffer = buffer;
    result.offset = 0;
    result.size = total_size;
    result.width = width;
    result.height = height;
    result.stride_y = stride_y;
    result.stride_uv = stride_uv;
    result.pts_us = static_cast<int64_t>(frame.u64PTS);
    result.mpp_info = MakeMppYuvFrameInfo(frame_info);
    return result;
}

}  // namespace hisisdk
}  // namespace live_stream
