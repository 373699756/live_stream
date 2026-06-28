#include "hisi_mpp_venc_capture.h"

#include "hisi_mpp_sdk.h"
#include "venc_packet_view.h"

#include "infra/clamp.h"
#include "infra/log.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/select.h>
#include <sys/time.h>
#include <utility>

namespace live_stream {
namespace hisisdk {
namespace venc_internal {
namespace {

struct VencStreamContext {
    int32_t chn = -1;
    VENC_CHN venc = 0;
    int fd = -1;
    StreamId stream_id = StreamId::kMain;
    Codec codec = Codec::kH264;
};

struct VencPayloadInfo {
    uint32_t size = 0;
};

void UpdateFrameTypeFromH264(H264E_NALU_TYPE_E type,
                             FrameType& frame_type) {
    if (frame_type == FrameType::kIdr) {
        return;
    }
    if (type == H264E_NALU_IDRSLICE) {
        frame_type = FrameType::kIdr;
    } else if (type == H264E_NALU_ISLICE) {
        frame_type = FrameType::kI;
    } else if (type == H264E_NALU_BSLICE && frame_type != FrameType::kI) {
        frame_type = FrameType::kB;
    }
}

void UpdateFrameTypeFromH265(H265E_NALU_TYPE_E type,
                             FrameType& frame_type) {
    if (frame_type == FrameType::kIdr) {
        return;
    }
    if (type == H265E_NALU_IDRSLICE) {
        frame_type = FrameType::kIdr;
    } else if (type == H265E_NALU_ISLICE) {
        frame_type = FrameType::kI;
    } else if (type == H265E_NALU_BSLICE && frame_type != FrameType::kI) {
        frame_type = FrameType::kB;
    }
}

void UpdateFrameTypeFromH264Pack(const VENC_PACK_S& pack,
                                 uint32_t data_num,
                                 FrameType& frame_type) {
    UpdateFrameTypeFromH264(pack.DataType.enH264EType, frame_type);
    for (uint32_t j = 0; frame_type != FrameType::kIdr && j < data_num; ++j) {
        UpdateFrameTypeFromH264(pack.stPackInfo[j].u32PackType.enH264EType,
                                frame_type);
    }
}

void UpdateFrameTypeFromH265Pack(const VENC_PACK_S& pack,
                                 uint32_t data_num,
                                 FrameType& frame_type) {
    UpdateFrameTypeFromH265(pack.DataType.enH265EType, frame_type);
    for (uint32_t j = 0; frame_type != FrameType::kIdr && j < data_num; ++j) {
        UpdateFrameTypeFromH265(pack.stPackInfo[j].u32PackType.enH265EType,
                                frame_type);
    }
}

FrameType FrameTypeFromStream(const VENC_STREAM_S& stream, Codec codec) {
    if (codec == Codec::kJpeg || codec == Codec::kMjpeg) {
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
        if (codec == Codec::kH264) {
            UpdateFrameTypeFromH264Pack(pack, data_num, frame_type);
        } else if (codec == Codec::kH265) {
            UpdateFrameTypeFromH265Pack(pack, data_num, frame_type);
        }
        if (frame_type == FrameType::kIdr) {
            return frame_type;
        }
    }

    return frame_type;
}

bool InitVencStreamContext(int32_t chn,
                           StreamId stream_id,
                           Codec codec,
                           VencStreamContext& context) {
    context.chn = chn;
    context.venc = static_cast<VENC_CHN>(chn);
    context.stream_id = stream_id;
    context.codec = codec;
    context.fd = HI_MPI_VENC_GetFd(context.venc);
    if (context.fd < 0) {
        Error("hisi_vendor",
              "HI_MPI_VENC_GetFd failed for channel %d", chn);
        return false;
    }
    if (context.fd >= FD_SETSIZE) {
        Error("hisi_vendor", "VENC fd %d exceeds FD_SETSIZE", context.fd);
        return false;
    }
    return true;
}

bool QueryVencStreamStatus(const VencStreamContext& context,
                           VENC_CHN_STATUS_S& status) {
    status = VENC_CHN_STATUS_S{};
    HI_S32 s32_ret = HI_MPI_VENC_QueryStatus(context.venc, &status);
    if (s32_ret != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_QueryStatus chn %d failed: 0x%08x",
              context.chn, s32_ret);
        return false;
    }
    return true;
}

bool GetVencStream(const VencStreamContext& context,
                   const VENC_CHN_STATUS_S& status,
                   VENC_PACK_S*& packs,
                   VENC_STREAM_S& stream) {
    if (status.u32CurPacks == 0) {
        return false;
    }

    packs = static_cast<VENC_PACK_S*>(
        std::calloc(status.u32CurPacks, sizeof(VENC_PACK_S)));
    if (packs == nullptr) {
        Error("hisi_vendor",
              "calloc VENC packs chn %d packs=%u failed",
              context.chn, status.u32CurPacks);
        return false;
    }

    stream = VENC_STREAM_S{};
    stream.pstPack = packs;
    stream.u32PackCount = status.u32CurPacks;
    const HI_S32 s32_ret = HI_MPI_VENC_GetStream(context.venc, &stream, 0);
    if (s32_ret != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_GetStream chn %d failed: 0x%08x",
              context.chn, s32_ret);
        std::free(packs);
        packs = nullptr;
        return false;
    }
    if (stream.u32PackCount > status.u32CurPacks) {
        Error(
            "hisi_vendor",
            "invalid VENC pack size chn=%d seq=%u packs=%u allocated=%u",
            context.chn, stream.u32Seq, stream.u32PackCount,
            status.u32CurPacks);
        (void)HI_MPI_VENC_ReleaseStream(context.venc, &stream);
        std::free(packs);
        packs = nullptr;
        return false;
    }
    return true;
}

VENC_STREAM_BUF_INFO_S GetVencStreamBufferInfo(VENC_CHN venc) {
    VENC_STREAM_BUF_INFO_S stream_buffer{};
    if (HI_MPI_VENC_GetStreamBufInfo(venc, &stream_buffer) != HI_SUCCESS) {
        stream_buffer = VENC_STREAM_BUF_INFO_S{};
    }
    return stream_buffer;
}

bool MeasureVencPayload(const VencStreamContext& context,
                        const VENC_STREAM_S& stream,
                        const VENC_STREAM_BUF_INFO_S& stream_buffer,
                        VencPayloadInfo& payload) {
    payload = VencPayloadInfo{};
    bool valid_stream = stream.u32PackCount > 0 && stream.pstPack != nullptr;
    for (uint32_t i = 0; valid_stream && i < stream.u32PackCount; ++i) {
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(stream.pstPack[i], stream_buffer,
                                         &packet_data)) {
            valid_stream = false;
            break;
        }
        if (payload.size > UINT32_MAX - packet_data.size) {
            valid_stream = false;
            break;
        }
        payload.size += packet_data.size;
    }
    if (!valid_stream || payload.size == 0) {
        Error("hisi_vendor",
              "invalid VENC stream chn=%d seq=%u packs=%u size=%u",
              context.chn, stream.u32Seq, stream.u32PackCount,
              payload.size);
        return false;
    }
    return true;
}

MediaBufferRef CopyVencPayload(
    const VencStreamContext& context,
    const VENC_STREAM_S& stream,
    const VENC_STREAM_BUF_INFO_S& stream_buffer,
    uint32_t payload_size) {
    MediaBufferBuilder buffer = MediaBufferBuilder::Allocate(payload_size);
    if (!buffer.Valid()) {
        Error("hisi_vendor",
              "alloc VENC payload chn=%d seq=%u size=%u failed",
              context.chn, stream.u32Seq, payload_size);
        return MediaBufferRef();
    }
    uint8_t* buffer_data = buffer.Data();
    if (buffer_data == nullptr) {
        return MediaBufferRef();
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
        const VENC_PACK_S& pack = stream.pstPack[i];
        internal::VencPacketData packet_data;
        if (!internal::GetVencPacketData(pack, stream_buffer, &packet_data)) {
            Error("hisi_vendor",
                  "copy VENC stream invalid pack chn=%d seq=%u "
                  "pack=%u len=%u offset=%u addr=%p",
                  context.chn, stream.u32Seq, i, pack.u32Len,
                  pack.u32Offset, static_cast<void*>(pack.pu8Addr));
            return MediaBufferRef();
        }
        if (packet_data.size > payload_size - offset) {
            Error("hisi_vendor",
                  "copy VENC stream overflow chn=%d seq=%u "
                  "offset=%u len=%u size=%u",
                  context.chn, stream.u32Seq, offset, packet_data.size,
                  payload_size);
            return MediaBufferRef();
        }
        if (packet_data.first.size > 0) {
            std::memcpy(buffer_data + offset, packet_data.first.data,
                        packet_data.first.size);
            offset += packet_data.first.size;
        }
        if (packet_data.second.size > 0) {
            std::memcpy(buffer_data + offset, packet_data.second.data,
                        packet_data.second.size);
            offset += packet_data.second.size;
        }
    }
    if (offset != payload_size || !buffer.Resize(offset)) {
        Error("hisi_vendor",
              "copy VENC stream chn=%d seq=%u size=%u expect=%u failed",
              context.chn, stream.u32Seq, offset, payload_size);
        return MediaBufferRef();
    }
    return buffer.Finish();
}

MediaFrame BuildMediaFrame(const VencStreamContext& context,
                           const VENC_STREAM_S& stream,
                           FrameType frame_type,
                           MediaBufferRef buffer) {
    MediaFrame frame;
    frame.stream_id = context.stream_id;
    frame.codec = context.codec;
    frame.frame_type = frame_type;
    frame.sequence = stream.u32Seq;
    frame.pts_us = stream.pstPack[0].u64PTS;
    frame.dts_us = frame.pts_us;
    frame.payload = std::move(buffer);
    return frame;
}

void ReleaseVencStream(const VencStreamContext& context,
                       VENC_STREAM_S& stream,
                       VENC_PACK_S* packs) {
    if (HI_MPI_VENC_ReleaseStream(context.venc, &stream) != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_ReleaseStream chn %d failed", context.chn);
    }
    std::free(packs);
}

void HandleVencStream(VencStreamContext& context,
                      MediaFrameCallback callback,
                      void* user) {
    VENC_CHN_STATUS_S status{};
    if (!QueryVencStreamStatus(context, status) || status.u32CurPacks == 0) {
        return;
    }

    VENC_PACK_S* packs = nullptr;
    VENC_STREAM_S stream{};
    if (!GetVencStream(context, status, packs, stream)) {
        return;
    }

    const VENC_STREAM_BUF_INFO_S stream_buffer =
        GetVencStreamBufferInfo(context.venc);
    VencPayloadInfo payload;
    if (!MeasureVencPayload(context, stream, stream_buffer, payload)) {
        ReleaseVencStream(context, stream, packs);
        return;
    }

    const FrameType frame_type = FrameTypeFromStream(stream, context.codec);
    MediaBufferRef buffer =
        CopyVencPayload(context, stream, stream_buffer, payload.size);
    if (!buffer.Valid()) {
        ReleaseVencStream(context, stream, packs);
        return;
    }
    MediaFrame frame =
        BuildMediaFrame(context, stream, frame_type, std::move(buffer));
    ReleaseVencStream(context, stream, packs);

    if (callback != nullptr) {
        callback(frame, user);
    }
}

}  // namespace

void VencStreamCapture::Run(MediaPipelineConfig config,
                            MediaFrameCallback callback,
                            void* user,
                            std::atomic<bool>& running) {
    VencStreamContext streams[2];
    uint32_t stream_size = 0;
    if (!InitVencStreamContext(config.venc_channel, StreamId::kMain,
                               config.main_stream.codec,
                               streams[stream_size])) {
        return;
    }
    ++stream_size;

    if (config.sub_stream.enabled) {
        if (!InitVencStreamContext(config.sub_venc_channel, StreamId::kSub,
                                   config.sub_stream.codec,
                                   streams[stream_size])) {
            return;
        }
        ++stream_size;
    }

    while (running.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = -1;
        for (uint32_t i = 0; i < stream_size; ++i) {
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
            Error("hisi_vendor", "select on VENC failed: %s",
                  strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;
        }

        for (uint32_t i = 0; i < stream_size; ++i) {
            if (FD_ISSET(streams[i].fd, &read_fds)) {
                HandleVencStream(streams[i], callback, user);
            }
        }
    }
}

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream
