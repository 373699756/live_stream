#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

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

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

bool CheckMpiCall(const char* expression, HI_S32 status) {
    if (status == HI_SUCCESS) {
        return true;
    }
    INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", expression, status);
    return false;
}

void ReleaseVpssMappedFrame(uint8_t* data, uint32_t capacity, void* user) {
    VpssMappedFrame* mapped_frame = static_cast<VpssMappedFrame*>(user);
    if (mapped_frame == nullptr) {
        return;
    }
    if (data != nullptr && capacity > 0) {
        const HI_S32 munmap_status = HI_MPI_SYS_Munmap(data, capacity);
        if (munmap_status != HI_SUCCESS) {
            INFRA_LOG_ERROR(
                "hisi_vendor",
                "CaptureYuvFrame: HI_MPI_SYS_Munmap failed: 0x%08x",
                munmap_status);
        }
    }
    const HI_S32 release_status = HI_MPI_VPSS_ReleaseChnFrame(
        mapped_frame->group, mapped_frame->channel, &mapped_frame->frame_info);
    if (release_status != HI_SUCCESS) {
        INFRA_LOG_ERROR(
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
    return qfactor > 99 ? 99 : qfactor;
}

bool EnsureVpssFrameDepth(VPSS_GRP group, VPSS_CHN channel) {
    if (channel >= VPSS_MAX_PHY_CHN_NUM) {
        VPSS_EXT_CHN_ATTR_S attr{};
        if (!CheckMpiCall("HI_MPI_VPSS_GetExtChnAttr",
                          HI_MPI_VPSS_GetExtChnAttr(group, channel, &attr))) {
            return false;
        }
        if (attr.u32Depth == 0) {
            attr.u32Depth = 2;
            return CheckMpiCall("HI_MPI_VPSS_SetExtChnAttr",
                                HI_MPI_VPSS_SetExtChnAttr(group, channel,
                                                          &attr));
        }
        return true;
    }

    VPSS_CHN_ATTR_S attr{};
    if (!CheckMpiCall("HI_MPI_VPSS_GetChnAttr",
                      HI_MPI_VPSS_GetChnAttr(group, channel, &attr))) {
        return false;
    }
    if (attr.u32Depth == 0) {
        attr.u32Depth = 2;
        return CheckMpiCall("HI_MPI_VPSS_SetChnAttr",
                            HI_MPI_VPSS_SetChnAttr(group, channel, &attr));
    }
    return true;
}

}  // namespace

JpegFrame MppHisiSdk::CaptureJpeg(const SnapshotConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    JpegFrame result{};
    result.width = config.size.width;
    result.height = config.size.height;

    if (!impl_->system_initialized_) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: system not initialized");
        return result;
    }
    if (config.jpeg_venc_channel < 0 || config.snap_vpss_group < 0 ||
        config.snap_vpss_channel < 0 || config.size.width == 0 ||
        config.size.height == 0 || config.timeout_ms == 0) {
        return result;
    }

    const VPSS_GRP vpss_group =
        static_cast<VPSS_GRP>(config.snap_vpss_group);
    const VPSS_CHN vpss_channel =
        static_cast<VPSS_CHN>(config.snap_vpss_channel);
    VIDEO_FRAME_INFO_S frame_info{};
    const HI_S32 timeout = static_cast<HI_S32>(config.timeout_ms);
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_GetChnFrame",
                      HI_MPI_VPSS_GetChnFrame(vpss_group, vpss_channel,
                                              &frame_info, timeout))) {
        return result;
    }

    const VIDEO_FRAME_S& source_frame = frame_info.stVFrame;
    if (source_frame.u32Width == 0 || source_frame.u32Height == 0) {
        (void)CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(vpss_group,
                                                       vpss_channel,
                                                       &frame_info));
        return result;
    }

    const VENC_CHN jpeg_chn = static_cast<VENC_CHN>(config.jpeg_venc_channel);
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
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_CreateChn",
                      HI_MPI_VENC_CreateChn(jpeg_chn, &attr))) {
        (void)CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(vpss_group,
                                                       vpss_channel,
                                                       &frame_info));
        return result;
    }

    VENC_CHN_PARAM_S channel_param{};
    channel_param.u32MaxStrmCnt = kJpegMaxStreamCount;
    channel_param.u32PollWakeUpFrmCnt = 1;
    channel_param.stFrameRate.s32SrcFrmRate = 1;
    channel_param.stFrameRate.s32DstFrmRate = 1;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_SetChnParam",
                      HI_MPI_VENC_SetChnParam(jpeg_chn, &channel_param))) {
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        (void)CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(vpss_group,
                                                       vpss_channel,
                                                       &frame_info));
        return result;
    }

    VENC_RECV_PIC_PARAM_S recv_param{};
    recv_param.s32RecvPicNum = -1;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_StartRecvFrame",
                      HI_MPI_VENC_StartRecvFrame(jpeg_chn, &recv_param))) {
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        (void)CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(vpss_group,
                                                       vpss_channel,
                                                       &frame_info));
        return result;
    }

    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_SendFrame",
                      HI_MPI_VENC_SendFrame(jpeg_chn, &frame_info, timeout))) {
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        (void)CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                           HI_MPI_VPSS_ReleaseChnFrame(vpss_group,
                                                       vpss_channel,
                                                       &frame_info));
        return result;
    }
    (void)CheckMpiCall("CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame",
                       HI_MPI_VPSS_ReleaseChnFrame(vpss_group, vpss_channel,
                                                   &frame_info));

    const int fd = HI_MPI_VENC_GetFd(jpeg_chn);
    if (fd < 0 || fd >= FD_SETSIZE) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "CaptureJpeg: HI_MPI_VENC_GetFd failed: %d", fd);
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return result;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    timeval select_timeout{};
    select_timeout.tv_sec = static_cast<time_t>(config.timeout_ms / 1000);
    select_timeout.tv_usec =
        static_cast<suseconds_t>((config.timeout_ms % 1000) * 1000);
    const int select_ret =
        select(fd + 1, &read_fds, nullptr, nullptr, &select_timeout);
    if (select_ret <= 0 || !FD_ISSET(fd, &read_fds)) {
        if (select_ret < 0) {
            INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: select failed: %s",
                            strerror(errno));
        } else {
            INFRA_LOG_ERROR("hisi_vendor",
                            "CaptureJpeg: timed out after %u ms",
                            config.timeout_ms);
        }
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return result;
    }

    VENC_CHN_STATUS_S status{};
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_QueryStatus",
                      HI_MPI_VENC_QueryStatus(jpeg_chn, &status))) {
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return result;
    }
    if (status.u32CurPacks == 0) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: no JPEG packs available");
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return result;
    }
    VENC_PACK_S* packs = static_cast<VENC_PACK_S*>(
        std::calloc(status.u32CurPacks, sizeof(VENC_PACK_S)));
    if (packs == nullptr) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "CaptureJpeg: calloc packs=%u failed",
                        status.u32CurPacks);
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return result;
    }

    VENC_STREAM_S stream{};
    stream.pstPack = packs;
    stream.u32PackCount = status.u32CurPacks;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_GetStream",
                      HI_MPI_VENC_GetStream(jpeg_chn, &stream, -1))) {
        std::free(packs);
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return result;
    }

    VENC_STREAM_BUF_INFO_S stream_buffer{};
    if (HI_MPI_VENC_GetStreamBufInfo(jpeg_chn, &stream_buffer) !=
        HI_SUCCESS) {
        stream_buffer = VENC_STREAM_BUF_INFO_S{};
    }

    uint32_t payload_size = 0;
    bool valid_stream = stream.u32PackCount > 0 &&
                        stream.u32PackCount <= status.u32CurPacks &&
                        stream.pstPack != nullptr;
    for (uint32_t i = 0; valid_stream && i < stream.u32PackCount; ++i) {
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(stream.pstPack[i], stream_buffer,
                                         &packet_data)) {
            valid_stream = false;
            break;
        }
        if (payload_size > UINT32_MAX - packet_data.size) {
            valid_stream = false;
            break;
        }
        payload_size += packet_data.size;
    }

    VideoBuffer* buffer = nullptr;
    uint32_t offset = 0;
    if (valid_stream && payload_size > 0) {
        buffer = VideoBufferAlloc(payload_size);
    }
    if (buffer != nullptr) {
        for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
            const VENC_PACK_S& pack = stream.pstPack[i];
            internal::VencPacketData packet_data;
            if (!internal::GetVencPacketData(pack, stream_buffer,
                                             &packet_data) ||
                packet_data.size > payload_size - offset) {
                valid_stream = false;
                break;
            }
            if (packet_data.first.size > 0) {
                std::memcpy(buffer->data + offset,
                            packet_data.first.data,
                            packet_data.first.size);
                offset += packet_data.first.size;
            }
            if (packet_data.second.size > 0) {
                std::memcpy(buffer->data + offset,
                            packet_data.second.data,
                            packet_data.second.size);
                offset += packet_data.second.size;
            }
        }
    }
    if (valid_stream && buffer != nullptr && offset == payload_size &&
        VideoBufferSetSize(buffer, offset)) {
        result.buffer = buffer;
        result.offset = 0;
        result.size = offset;
        result.pts_us = stream.pstPack[0].u64PTS;
    } else {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: invalid JPEG stream");
        VideoBufferRelease(buffer);
    }

    const HI_S32 release_status =
        HI_MPI_VENC_ReleaseStream(jpeg_chn, &stream);
    if (release_status != HI_SUCCESS) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "CaptureJpeg: HI_MPI_VENC_ReleaseStream failed: 0x%08x",
            release_status);
    }
    std::free(packs);
    (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
    (void)HI_MPI_VENC_DestroyChn(jpeg_chn);

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
        INFRA_LOG_ERROR("hisi_vendor", "CaptureYuvFrame: system not initialized");
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
    if (!CheckMpiCall("CaptureYuvFrame: HI_MPI_VPSS_GetChnFrame",
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
        INFRA_LOG_ERROR(
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
        INFRA_LOG_ERROR(
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
    return result;
}

}  // namespace hisisdk
}  // namespace live_stream
