#include "hisi_sdk_default.h"

#include <cstring>
#include <memory>

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
extern "C" {
#include "hi_comm_snap.h"
#include "mpi_snap.h"
}
#endif

namespace live_stream {
namespace hisisdk {
namespace {

constexpr uint32_t kDefaultJpegPoolBlocks = 2;

bool IsValidSize(const Size& size) {
    return size.width > 0 && size.height > 0;
}

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const uint32_t remainder = value % alignment;
    return remainder == 0 ? value : value + alignment - remainder;
}

uint32_t EstimateJpegBlockSize(const SnapshotConfig& config) {
    const uint64_t pixels =
        static_cast<uint64_t>(config.size.width) * config.size.height;
    uint64_t block_size = pixels;
    if (block_size < 1024U * 1024U) {
        block_size = 1024U * 1024U;
    }
    if (block_size > 16U * 1024U * 1024U) {
        block_size = 16U * 1024U * 1024U;
    }
    return AlignUp(static_cast<uint32_t>(block_size), 4096);
}

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
infra::Status FromHiStatus(int32_t status) {
    return status == HI_SUCCESS ? infra::Status::kOk : infra::Status::kIoError;
}

infra::Status StartSnapshotPipe(const SnapshotConfig& config) {
    SNAP_ATTR_S attr{};
    attr.enSnapType = SNAP_TYPE_NORMAL;
    attr.bLoadCCM = config.load_ccm ? HI_TRUE : HI_FALSE;
    attr.stNormalAttr.u32FrameCnt = config.frame_count;
    attr.stNormalAttr.u32RepeatSendTimes = config.repeat_send_times;
    attr.stNormalAttr.bZSL = config.zero_shutter_lag ? HI_TRUE : HI_FALSE;

    int32_t ret = HI_MPI_SNAP_SetPipeAttr(config.snap_pipe, &attr);
    if (ret != HI_SUCCESS) {
        return FromHiStatus(ret);
    }
    ret = HI_MPI_SNAP_EnablePipe(config.snap_pipe);
    if (ret != HI_SUCCESS) {
        return FromHiStatus(ret);
    }
    ret = HI_MPI_SNAP_TriggerPipe(config.snap_pipe);
    return FromHiStatus(ret);
}

void CleanupSnapshotPipe(const SnapshotConfig& config) {
    HI_MPI_SNAP_DisablePipe(config.snap_pipe);
}

infra::Result<JpegFrame> ReadSnapshotStream(const SnapshotConfig& config) {
    (void)config;
    return infra::Result<JpegFrame>::Fail(infra::Status::kNotSupported);
}
#endif

infra::Result<JpegFrame> MakeHostJpeg(const SnapshotConfig& config) {
    static const uint8_t kMinimalJpeg[] = {0xff, 0xd8, 0xff, 0xd9};
    std::shared_ptr<infra::IMediaBufferPool> pool =
        infra::CreateFixedMediaBufferPool(
            EstimateJpegBlockSize(config), kDefaultJpegPoolBlocks);
    if (!pool) {
        return infra::Result<JpegFrame>::Fail(infra::Status::kNoMemory);
    }
    std::shared_ptr<infra::IMediaBuffer> buffer = pool->Acquire();
    if (!buffer || buffer->Capacity() < sizeof(kMinimalJpeg)) {
        return infra::Result<JpegFrame>::Fail(infra::Status::kNoMemory);
    }
    std::memcpy(buffer->MutableData(), kMinimalJpeg, sizeof(kMinimalJpeg));
    buffer->SetSize(static_cast<uint32_t>(sizeof(kMinimalJpeg)));

    JpegFrame frame;
    frame.buffer = buffer;
    frame.offset = 0;
    frame.size = buffer->Size();
    frame.width = config.size.width;
    frame.height = config.size.height;
    frame.pts_us = 0;
    return infra::Result<JpegFrame>::Ok(frame);
}

}  // namespace

infra::Result<JpegFrame> DefaultHisiSdk::CaptureJpeg(
    const SnapshotConfig& config) {
    if (!IsValidSize(config.size) || config.timeout_ms == 0) {
        return infra::Result<JpegFrame>::Fail(infra::Status::kInvalidParam);
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    const infra::Status start_status = StartSnapshotPipe(config);
    if (start_status != infra::Status::kOk) {
        CleanupSnapshotPipe(config);
        return infra::Result<JpegFrame>::Fail(start_status);
    }
    infra::Result<JpegFrame> frame = ReadSnapshotStream(config);
    CleanupSnapshotPipe(config);
    return frame;
#else
    return MakeHostJpeg(config);
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
