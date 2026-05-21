#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sys/select.h>
#include <sys/time.h>
#include <utility>
#include <vector>

namespace live_stream {
namespace hisisdk {
namespace {

constexpr uint32_t kMaxJpegPacksPerFrame = 16;
constexpr uint32_t kMaxYuvCaptureBytes = 64U * 1024U * 1024U;
constexpr uint32_t kDefaultJpegQuality = 50;
constexpr uint32_t kJpegMaxStreamCount = 200;
constexpr uint32_t kJpegIpQpDelta = 2;

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

uint32_t VencPackDataLen(const VENC_PACK_S& pack) {
    if (pack.pu8Addr == nullptr || pack.u32Len <= pack.u32Offset) {
        return 0;
    }
    return pack.u32Len - pack.u32Offset;
}

bool CopyJpegStreamPayloads(const VENC_STREAM_S& stream,
                            std::shared_ptr<IMediaBuffer>* buffer,
                            uint32_t* size) {
    if (buffer == nullptr || size == nullptr || stream.pstPack == nullptr ||
        stream.u32PackCount == 0) {
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

bool CreateJpegCaptureChannel(VENC_CHN jpeg_chn,
                              const VIDEO_FRAME_S& frame,
                              uint32_t jpeg_quality) {
    if (frame.u32Width == 0 || frame.u32Height == 0) {
        return false;
    }

    VENC_CHN_ATTR_S attr{};
    attr.stVencAttr.enType = PT_MJPEG;
    attr.stVencAttr.u32MaxPicWidth = frame.u32Width;
    attr.stVencAttr.u32MaxPicHeight = frame.u32Height;
    attr.stVencAttr.u32PicWidth = frame.u32Width;
    attr.stVencAttr.u32PicHeight = frame.u32Height;
    attr.stVencAttr.u32BufSize =
        AlignUp(frame.u32Width, 32) * AlignUp(frame.u32Height, 32) * 2;
    attr.stVencAttr.bByFrame = HI_TRUE;
    attr.stVencAttr.u32Profile = 0;
    attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
    attr.stRcAttr.stMjpegFixQp.u32SrcFrameRate = 1;
    attr.stRcAttr.stMjpegFixQp.fr32DstFrameRate = 1;
    attr.stRcAttr.stMjpegFixQp.u32Qfactor =
        JpegQualityFromConfig(jpeg_quality);
    attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attr.stGopAttr.stNormalP.s32IPQpDelta = kJpegIpQpDelta;

    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_CreateChn",
                      HI_MPI_VENC_CreateChn(jpeg_chn, &attr))) {
        return false;
    }

    VENC_CHN_PARAM_S channel_param{};
    channel_param.u32MaxStrmCnt = kJpegMaxStreamCount;
    channel_param.u32PollWakeUpFrmCnt = 1;
    channel_param.stFrameRate.s32SrcFrmRate = 1;
    channel_param.stFrameRate.s32DstFrmRate = 1;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_SetChnParam",
                      HI_MPI_VENC_SetChnParam(jpeg_chn, &channel_param))) {
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return false;
    }

    VENC_RECV_PIC_PARAM_S recv_param{};
    recv_param.s32RecvPicNum = -1;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_StartRecvFrame",
                      HI_MPI_VENC_StartRecvFrame(jpeg_chn, &recv_param))) {
        (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
        return false;
    }

    return true;
}

void DestroyJpegCaptureChannel(VENC_CHN jpeg_chn) {
    (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
    (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
}

bool WaitJpegStreamReady(VENC_CHN jpeg_chn, uint32_t timeout_ms) {
    const int fd = HI_MPI_VENC_GetFd(jpeg_chn);
    if (fd < 0 || fd >= FD_SETSIZE) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "CaptureJpeg: HI_MPI_VENC_GetFd failed: %d", fd);
        return false;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    timeout.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    const int ret = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ret < 0) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: select failed: %s",
                        strerror(errno));
        return false;
    }
    if (ret == 0) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: timed out after %u ms",
                        timeout_ms);
        return false;
    }
    if (!FD_ISSET(fd, &read_fds)) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: fd %d not ready", fd);
        return false;
    }
    return true;
}

void ReleaseVpssFrame(VPSS_GRP group,
                      VPSS_CHN channel,
                      VIDEO_FRAME_INFO_S* frame) {
    if (frame == nullptr) {
        return;
    }
    const HI_S32 status = HI_MPI_VPSS_ReleaseChnFrame(group, channel, frame);
    if (status != HI_SUCCESS) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "CaptureJpeg: HI_MPI_VPSS_ReleaseChnFrame failed: 0x%08x",
            status);
    }
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

    INFRA_LOG_INFO(
        "hisi_vendor",
        "CaptureJpeg: source VPSS grp=%d chn=%d to MJPEG venc=%d size=%ux%u "
        "timeout=%u requested_quality=%u",
        config.snap_vpss_group, config.snap_vpss_channel,
        config.jpeg_venc_channel, config.size.width, config.size.height,
        config.timeout_ms, config.jpeg_quality);

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

    const VENC_CHN jpeg_chn = static_cast<VENC_CHN>(config.jpeg_venc_channel);
    VENC_STREAM_S stream{};
    std::vector<VENC_PACK_S> packs;

    if (!CreateJpegCaptureChannel(jpeg_chn, frame_info.stVFrame,
                                  config.jpeg_quality)) {
        ReleaseVpssFrame(vpss_group, vpss_channel, &frame_info);
        return result;
    }

    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_SendFrame",
                      HI_MPI_VENC_SendFrame(jpeg_chn, &frame_info, timeout))) {
        DestroyJpegCaptureChannel(jpeg_chn);
        ReleaseVpssFrame(vpss_group, vpss_channel, &frame_info);
        return result;
    }
    ReleaseVpssFrame(vpss_group, vpss_channel, &frame_info);

    if (!WaitJpegStreamReady(jpeg_chn, config.timeout_ms)) {
        DestroyJpegCaptureChannel(jpeg_chn);
        return result;
    }

    VENC_CHN_STATUS_S status{};
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_QueryStatus",
                      HI_MPI_VENC_QueryStatus(jpeg_chn, &status))) {
        DestroyJpegCaptureChannel(jpeg_chn);
        return result;
    }
    INFRA_LOG_INFO(
        "hisi_vendor",
        "CaptureJpeg: JPEG venc=%d status packs=%u left_pics=%u "
        "left_frames=%u",
        config.jpeg_venc_channel, status.u32CurPacks, status.u32LeftPics,
        status.u32LeftStreamFrames);
    if (status.u32CurPacks == 0 || status.u32LeftStreamFrames == 0) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: no JPEG packs available");
        DestroyJpegCaptureChannel(jpeg_chn);
        return result;
    }
    if (status.u32CurPacks > kMaxJpegPacksPerFrame) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "CaptureJpeg: too many JPEG packs %u max=%u",
                        status.u32CurPacks, kMaxJpegPacksPerFrame);
        DestroyJpegCaptureChannel(jpeg_chn);
        return result;
    }

    packs.resize(status.u32CurPacks);
    stream.pstPack = packs.data();
    stream.u32PackCount = status.u32CurPacks;
    INFRA_LOG_INFO("hisi_vendor",
                   "CaptureJpeg: JPEG venc=%d get stream begin packs=%u",
                   config.jpeg_venc_channel, stream.u32PackCount);
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_GetStream",
                      HI_MPI_VENC_GetStream(jpeg_chn, &stream, -1))) {
        DestroyJpegCaptureChannel(jpeg_chn);
        return result;
    }
    INFRA_LOG_INFO(
        "hisi_vendor",
        "CaptureJpeg: JPEG venc=%d get stream ok seq=%u packs=%u first_len=%u "
        "first_offset=%u first_addr=%p",
        config.jpeg_venc_channel, stream.u32Seq, stream.u32PackCount,
        stream.u32PackCount > 0 ? stream.pstPack[0].u32Len : 0,
        stream.u32PackCount > 0 ? stream.pstPack[0].u32Offset : 0,
        stream.u32PackCount > 0 ? stream.pstPack[0].pu8Addr : nullptr);

    std::shared_ptr<IMediaBuffer> buffer;
    uint32_t size = 0;
    if (CopyJpegStreamPayloads(stream, &buffer, &size)) {
        result.buffer = std::move(buffer);
        result.offset = 0;
        result.size = size;
        result.pts_us = stream.pstPack[0].u64PTS;
        INFRA_LOG_INFO("hisi_vendor",
                       "CaptureJpeg: JPEG copied size=%u pts=%lld",
                       result.size, static_cast<long long>(result.pts_us));
    } else {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: invalid JPEG stream");
    }

    const HI_S32 release_status =
        HI_MPI_VENC_ReleaseStream(jpeg_chn, &stream);
    if (release_status != HI_SUCCESS) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "CaptureJpeg: HI_MPI_VENC_ReleaseStream failed: 0x%08x",
            release_status);
    }
    DestroyJpegCaptureChannel(jpeg_chn);

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
    const uint64_t y_size64 = static_cast<uint64_t>(stride_y) * height;
    const uint64_t uv_size64 = static_cast<uint64_t>(stride_uv) * height / 2;
    const uint64_t total_size64 = y_size64 + uv_size64;
    const uint64_t uv_offset64 =
        frame.u64PhyAddr[1] > frame.u64PhyAddr[0]
            ? frame.u64PhyAddr[1] - frame.u64PhyAddr[0]
            : y_size64;
    const uint64_t map_size64 = uv_offset64 + uv_size64;
    if (stride_y == 0 || width == 0 || height == 0 || total_size64 == 0 ||
        total_size64 > kMaxYuvCaptureBytes ||
        map_size64 > kMaxYuvCaptureBytes ||
        total_size64 > std::numeric_limits<uint32_t>::max() ||
        map_size64 > std::numeric_limits<uint32_t>::max() ||
        frame.u64PhyAddr[0] == 0 || frame.u64PhyAddr[1] == 0 ||
        map_size64 < uv_offset64) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "CaptureYuvFrame: invalid frame grp=%d chn=%d width=%u height=%u "
            "stride_y=%u stride_uv=%u phy0=0x%llx phy1=0x%llx vir=0x%llx "
            "total=%llu map=%llu",
            vpss_channel.device, vpss_channel.channel, width, height,
            stride_y, stride_uv,
            static_cast<unsigned long long>(frame.u64PhyAddr[0]),
            static_cast<unsigned long long>(frame.u64PhyAddr[1]),
            static_cast<unsigned long long>(frame.u64VirAddr[0]),
            static_cast<unsigned long long>(total_size64),
            static_cast<unsigned long long>(map_size64));
        (void)HI_MPI_VPSS_ReleaseChnFrame(group, channel, &frame_info);
        return result;
    }
    const uint32_t y_size = static_cast<uint32_t>(y_size64);
    const uint32_t uv_size = static_cast<uint32_t>(uv_size64);
    const uint32_t total_size = static_cast<uint32_t>(total_size64);
    const uint32_t uv_offset = static_cast<uint32_t>(uv_offset64);
    const uint32_t map_size = static_cast<uint32_t>(map_size64);
    std::shared_ptr<IMediaBuffer> buffer = CreateMediaBuffer(total_size);
    void* mapped = nullptr;
    if (buffer) {
        mapped = HI_MPI_SYS_Mmap(frame.u64PhyAddr[0], map_size);
    }
    if (buffer && mapped != nullptr) {
        const uint8_t* mapped_y = static_cast<const uint8_t*>(mapped);
        const uint8_t* mapped_uv = mapped_y + uv_offset;
        std::memcpy(buffer->MutableData(), mapped_y, y_size);
        std::memcpy(buffer->MutableData() + y_size, mapped_uv, uv_size);
        if (buffer->SetSize(total_size)) {
            result.buffer = buffer;
            result.offset = 0;
            result.size = total_size;
            result.width = width;
            result.height = height;
            result.stride_y = stride_y;
            result.stride_uv = stride_uv;
            result.pts_us = static_cast<int64_t>(frame.u64PTS);
        }
    } else {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "CaptureYuvFrame: mmap/copy skipped grp=%d chn=%d phy=0x%llx "
            "vir=0x%llx size=%u map=%u buffer=%d mapped=%d",
            vpss_channel.device, vpss_channel.channel,
            static_cast<unsigned long long>(frame.u64PhyAddr[0]),
            static_cast<unsigned long long>(frame.u64VirAddr[0]), total_size,
            map_size, buffer ? 1 : 0, mapped != nullptr ? 1 : 0);
    }
    if (mapped != nullptr) {
        const HI_S32 munmap_status = HI_MPI_SYS_Munmap(mapped, map_size);
        if (munmap_status != HI_SUCCESS) {
            INFRA_LOG_ERROR(
                "hisi_vendor",
                "CaptureYuvFrame: HI_MPI_SYS_Munmap failed: 0x%08x",
                munmap_status);
        }
    }
    const HI_S32 release_status =
        HI_MPI_VPSS_ReleaseChnFrame(group, channel, &frame_info);
    if (release_status != HI_SUCCESS) {
        INFRA_LOG_ERROR(
            "hisi_vendor",
            "CaptureYuvFrame: HI_MPI_VPSS_ReleaseChnFrame failed: 0x%08x",
            release_status);
    }
    return result;
}

}  // namespace hisisdk
}  // namespace live_stream
