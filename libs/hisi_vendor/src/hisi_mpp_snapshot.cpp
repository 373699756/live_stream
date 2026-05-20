#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <poll.h>
#include <vector>

namespace live_stream {
namespace hisisdk {
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

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

void CleanupJpegCapture(VENC_CHN jpeg_chn, const MPP_CHN_S& src,
                        const MPP_CHN_S& dst, bool bound, bool receiving) {
    if (receiving) {
        (void)HI_MPI_VENC_StopRecvFrame(jpeg_chn);
    }
    if (bound) {
        (void)HI_MPI_SYS_UnBind(&src, &dst);
    }
    (void)HI_MPI_VENC_DestroyChn(jpeg_chn);
}

}  // namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

JpegFrame MppHisiSdk::CaptureJpeg(const SnapshotConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    JpegFrame result{};
    result.width = config.size.width;
    result.height = config.size.height;

    if (!impl_->system_initialized_) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: system not initialized");
        return result;
    }

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    VENC_CHN jpeg_chn = static_cast<VENC_CHN>(config.jpeg_venc_channel);
    MPP_CHN_S src{};
    MPP_CHN_S dst{};
    bool bound = false;
    bool receiving = false;

    // ─── 1. Create JPEG VENC channel ──────────────────────────
    VENC_CHN_ATTR_S attr{};
    attr.stVencAttr.enType = PT_JPEG;
    attr.stVencAttr.u32MaxPicWidth = config.size.width;
    attr.stVencAttr.u32MaxPicHeight = config.size.height;
    attr.stVencAttr.u32PicWidth = config.size.width;
    attr.stVencAttr.u32PicHeight = config.size.height;
    attr.stVencAttr.u32BufSize = config.size.width * config.size.height * 2;
    attr.stVencAttr.bByFrame = HI_TRUE;
    attr.stVencAttr.stAttrJpege.bSupportDCF = HI_FALSE;
    attr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
    attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
    attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attr.stGopAttr.stNormalP.s32IPQpDelta = 0;

    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_CreateChn",
                      HI_MPI_VENC_CreateChn(jpeg_chn, &attr))) {
        return result;
    }

    INFRA_LOG_INFO(
        "hisi_vendor",
        "CaptureJpeg: bind VPSS grp=%d chn=%d to JPEG venc=%d size=%ux%u "
        "timeout=%u requested_quality=%u",
        config.snap_vpss_group, config.snap_vpss_channel,
        config.jpeg_venc_channel, config.size.width, config.size.height,
        config.timeout_ms, config.jpeg_quality);

    src.enModId = HI_ID_VPSS;
    src.s32DevId = config.snap_vpss_group;
    src.s32ChnId = config.snap_vpss_channel;

    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = config.jpeg_venc_channel;

    if (!CheckMpiCall("CaptureJpeg: HI_MPI_SYS_Bind",
                      HI_MPI_SYS_Bind(&src, &dst))) {
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }
    bound = true;
    INFRA_LOG_INFO("hisi_vendor", "CaptureJpeg: VPSS bound to JPEG venc=%d",
                   config.jpeg_venc_channel);

    VENC_RECV_PIC_PARAM_S recv_param{};
    recv_param.s32RecvPicNum = 1;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_StartRecvFrame",
                      HI_MPI_VENC_StartRecvFrame(jpeg_chn, &recv_param))) {
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }
    receiving = true;
    INFRA_LOG_INFO("hisi_vendor", "CaptureJpeg: JPEG venc=%d receiving started",
                   config.jpeg_venc_channel);

    const int fd = HI_MPI_VENC_GetFd(jpeg_chn);
    if (fd < 0) {
        INFRA_LOG_ERROR("hisi_vendor",
                        "CaptureJpeg: HI_MPI_VENC_GetFd failed: %d", fd);
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN | POLLERR;
    const int timeout_ms = static_cast<int>(config.timeout_ms);
    const int poll_status = poll(&pfd, 1, timeout_ms);
    INFRA_LOG_INFO("hisi_vendor",
                   "CaptureJpeg: JPEG venc=%d poll status=%d revents=0x%x",
                   config.jpeg_venc_channel, poll_status, pfd.revents);
    if (poll_status <= 0) {
        if (poll_status < 0) {
            INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: poll failed: %s",
                            strerror(errno));
        } else {
            INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: timed out after %u ms",
                            config.timeout_ms);
        }
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }
    if ((pfd.revents & POLLERR) != 0) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: POLLERR on VENC fd %d", fd);
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }

    // ─── 4. Query and get JPEG stream ─────────────────────────
    VENC_CHN_STATUS_S status{};
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_QueryStatus",
                      HI_MPI_VENC_QueryStatus(jpeg_chn, &status))) {
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }
    INFRA_LOG_INFO(
        "hisi_vendor",
        "CaptureJpeg: JPEG venc=%d status packs=%u left_pics=%u "
        "left_frames=%u",
        config.jpeg_venc_channel, status.u32CurPacks, status.u32LeftPics,
        status.u32LeftStreamFrames);
    if (status.u32CurPacks == 0) {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: no JPEG packs available");
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }

    std::vector<VENC_PACK_S> packs(status.u32CurPacks);
    VENC_STREAM_S stream{};
    stream.pstPack = packs.data();
    stream.u32PackCount = status.u32CurPacks;
    if (!CheckMpiCall("CaptureJpeg: HI_MPI_VENC_GetStream",
                      HI_MPI_VENC_GetStream(jpeg_chn, &stream, -1))) {
        CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);
        return result;
    }

    uint64_t total_len = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        total_len += VencPackDataLen(stream.pstPack[i]);
    }
    if (total_len > 0 && total_len <= std::numeric_limits<uint32_t>::max()) {
        auto buffer = std::make_shared<internal::HeapMediaBuffer>(
            static_cast<uint32_t>(total_len));
        uint32_t offset = 0;
        for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
            const VENC_PACK_S& pack = stream.pstPack[i];
            const uint32_t data_len = VencPackDataLen(pack);
            if (data_len == 0) {
                continue;
            }
            std::memcpy(buffer->MutableData() + offset,
                        static_cast<const uint8_t*>(pack.pu8Addr) + pack.u32Offset,
                        data_len);
            offset += data_len;
        }
        buffer->SetSize(offset);

        result.buffer = std::move(buffer);
        result.offset = 0;
        result.size = offset;
        result.pts_us = stream.pstPack[0].u64PTS;
    } else {
        INFRA_LOG_ERROR("hisi_vendor", "CaptureJpeg: invalid JPEG size %llu",
                        static_cast<unsigned long long>(total_len));
    }

    (void)HI_MPI_VENC_ReleaseStream(jpeg_chn, &stream);
    CleanupJpegCapture(jpeg_chn, src, dst, bound, receiving);

    return result;

#else
    (void)config;
    return result;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
