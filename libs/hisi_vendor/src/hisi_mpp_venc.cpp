#include "hisi_vendor/mpp_sdk.h"
#include "hisi_mpp_sdk.h"
#include "hisi_mpp_venc_channel.h"
#include "mpp_hisi_sdk_impl.h"
#include "venc_config.h"
#include "venc_packet_view.h"

#include "infra/clamp.h"
#include "infra/log.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <sys/select.h>
#include <sys/time.h>
#include <thread>
#include <utility>

namespace live_stream {
namespace hisisdk {
using venc_internal::ApplyVencRoiConfig;
using venc_internal::CodecName;
using venc_internal::GopModeName;
using venc_internal::RcModeName;
using venc_internal::VencChannelControl;

namespace {

void UpdateFrameTypeFromH264(H264E_NALU_TYPE_E type, FrameType& frame_type) {
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

void UpdateFrameTypeFromH265(H265E_NALU_TYPE_E type, FrameType& frame_type) {
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
        Error("hisi_vendor",
              "VENC fd %d exceeds FD_SETSIZE", context.fd);
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
    // HI_MPI_VENC_GetStream 返回的 pack 指针和数据仍归 MPP/VENC 管理；
    // 在 HI_MPI_VENC_ReleaseStream 之后不能再引用这些地址。
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
    if (HI_MPI_VENC_GetStreamBufInfo(venc, &stream_buffer) !=
        HI_SUCCESS) {
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
        // 一个 VENC pack 可能因为环形码流 buffer 回绕被拆成 first/second 两段。
        // 这里先只测量总长度，真正复制时再按同样切片顺序拷贝。
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

MediaBufferRef CopyVencPayload(const VencStreamContext& context,
                               const VENC_STREAM_S& stream,
                               const VENC_STREAM_BUF_INFO_S& stream_buffer,
                               uint32_t payload_size) {
    // 这是从 HiSilicon VENC 内部 stream buffer 到项目内存的唯一深拷贝点。
    // 复制完成后 MediaFrame 持有 MediaBuffer；随后即可 ReleaseStream，把
    // MPP 的 pack buffer 还给驱动，不影响上层继续发送该帧。
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
                  context.chn, stream.u32Seq, offset,
                  packet_data.size, payload_size);
            return MediaBufferRef();
        }
        if (packet_data.first.size > 0) {
            // first/second 都是 VENC ring buffer 中的只读片段，按 AnnexB 原顺序
            // 拼进一个连续 MediaBuffer，便于后续 parser/packetizer 直接使用。
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
              "copy VENC stream chn=%d seq=%u size=%u expect=%u "
              "failed",
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
    // PTS 取自 VENC pack；media 后续会修正为从流起点开始的单调相对时间。
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
              "HI_MPI_VENC_ReleaseStream chn %d failed",
              context.chn);
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
    // frame 已经拥有项目 MediaBuffer。ReleaseVencStream 只释放 MPP stream 和
    // 临时 pack 数组，不会释放 frame.payload 持有的 MediaBuffer。
    ReleaseVencStream(context, stream, packs);

    if (callback != nullptr) {
        callback(frame, user);
    }
}

// Match the HiSilicon sample flow: one capture thread selects all VENC fds.
void VencStreamLoop(MediaPipelineConfig config,
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

}  // anonymous namespace

// ====================================================================
// StartVenc / StopVenc
// ====================================================================
bool MppHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    const bool need_sub_stream = config.sub_stream.enabled;
    const bool all_required_channels_created =
        VencChannelControl::Matches(impl_->main_venc_, config.venc_channel,
                                    config.vpss_group, config.vpss_channel,
                                    config.main_stream) &&
        (need_sub_stream
             ? VencChannelControl::Matches(
                   impl_->sub_venc_, config.sub_venc_channel,
                   config.vpss_group, config.sub_vpss_channel,
                   config.sub_stream)
             : !VencChannelControl::IsCreated(impl_->sub_venc_));
    if (all_required_channels_created) {
        return true;
    }
    if (VencChannelControl::IsCreated(impl_->main_venc_) ||
        VencChannelControl::IsCreated(impl_->sub_venc_)) {
        if (impl_->stream_running_.load() || impl_->stream_thread_.joinable()) {
            Error("hisi_vendor",
                  "reconfigure VENC while stream thread is running");
            return false;
        }
        VencChannelControl::Destroy(impl_->sub_venc_);
        VencChannelControl::Destroy(impl_->main_venc_);
    }

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

    VencChannelControl::Init(impl_->main_venc_, StreamId::kMain,
                             config.venc_channel, config.vpss_group,
                             config.vpss_channel, config.main_stream.codec);
    if (!VencChannelControl::Create(impl_->main_venc_, config.main_stream)) {
        Error(
            "hisi_vendor",
            "start main VENC failed chn=%d codec=%s rc=%s gop_mode=%s "
            "size=%ux%u src_fps=%d dst_fps=%d bitrate=%u gop=%u",
            config.venc_channel, CodecName(config.main_stream.codec),
            RcModeName(config.main_stream.rc_mode),
            GopModeName(config.main_stream.gop_mode),
            config.main_stream.size.width, config.main_stream.size.height,
            config.main_stream.frame_rate.source_fps,
            config.main_stream.frame_rate.target_fps,
            config.main_stream.bitrate_kbps, config.main_stream.gop);
        VencChannelControl::Reset(impl_->main_venc_);
        return false;
    }

    if (need_sub_stream) {
        VencChannelControl::Init(impl_->sub_venc_, StreamId::kSub,
                                 config.sub_venc_channel, config.vpss_group,
                                 config.sub_vpss_channel,
                                 config.sub_stream.codec);
        if (!VencChannelControl::Create(impl_->sub_venc_,
                                        config.sub_stream)) {
            VencChannelControl::Destroy(impl_->main_venc_);
            VencChannelControl::Reset(impl_->sub_venc_);
            Error(
                "hisi_vendor",
                "start sub VENC failed chn=%d codec=%s rc=%s gop_mode=%s "
                "size=%ux%u src_fps=%d dst_fps=%d bitrate=%u gop=%u",
                config.sub_venc_channel, CodecName(config.sub_stream.codec),
                RcModeName(config.sub_stream.rc_mode),
                GopModeName(config.sub_stream.gop_mode),
                config.sub_stream.size.width, config.sub_stream.size.height,
                config.sub_stream.frame_rate.source_fps,
                config.sub_stream.frame_rate.target_fps,
                config.sub_stream.bitrate_kbps, config.sub_stream.gop);
            return false;
        }
    } else {
        VencChannelControl::Reset(impl_->sub_venc_);
    }

    return true;
}

void StopVencStreamThread(MppHisiSdkImpl& impl) {
    if (!impl.stream_running_.load() && !impl.stream_thread_.joinable()) {
        return;
    }

    impl.stream_running_.store(false);

    if (impl.stream_thread_.joinable()) {
        impl.stream_thread_.join();
    }

    impl.frame_callback_ = nullptr;
    impl.frame_callback_user_ = nullptr;
}

void DestroyVencChannels(MppHisiSdkImpl& impl) {
    VencChannelControl::Destroy(impl.sub_venc_);
    VencChannelControl::Destroy(impl.main_venc_);
}

void MppHisiSdk::StopVenc(const MediaPipelineConfig& config) {
    (void)config;
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    StopVencStreamThread(*impl_);
    DestroyVencChannels(*impl_);
}

// ====================================================================
// Bind VPSS → VENC
// ====================================================================
bool MppHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    const bool need_sub_stream = config.sub_stream.enabled;
    const bool all_required_channels_bound =
        VencChannelControl::IsBoundToVpss(impl_->main_venc_) &&
        (need_sub_stream
             ? VencChannelControl::IsBoundToVpss(impl_->sub_venc_)
             : !VencChannelControl::IsBoundToVpss(impl_->sub_venc_));
    if (all_required_channels_bound) {
        return true;
    }
    if (!VencChannelControl::IsCreated(impl_->main_venc_) ||
        (need_sub_stream &&
         !VencChannelControl::IsCreated(impl_->sub_venc_))) {
        Error("hisi_vendor", "bind VPSS to VENC before VENC is created");
        return false;
    }

    if (!VencChannelControl::BindToVpss(impl_->main_venc_)) {
        Error("hisi_vendor",
              "bind main VPSS to VENC failed vpss=%d:%d venc=%d",
              config.vpss_group, config.vpss_channel,
              config.venc_channel);
        return false;
    }
    if (!VencChannelControl::StartRecv(impl_->main_venc_)) {
        VencChannelControl::UnbindFromVpss(impl_->main_venc_);
        Error("hisi_vendor", "start main VENC recv failed chn=%d",
              config.venc_channel);
        return false;
    }

    if (need_sub_stream) {
        if (!VencChannelControl::BindToVpss(impl_->sub_venc_)) {
            VencChannelControl::UnbindFromVpss(impl_->main_venc_);
            Error("hisi_vendor",
                  "bind sub VPSS to VENC failed vpss=%d:%d venc=%d",
                  config.vpss_group, config.sub_vpss_channel,
                  config.sub_venc_channel);
            return false;
        }
        if (!VencChannelControl::StartRecv(impl_->sub_venc_)) {
            VencChannelControl::UnbindFromVpss(impl_->sub_venc_);
            VencChannelControl::UnbindFromVpss(impl_->main_venc_);
            Error("hisi_vendor", "start sub VENC recv failed chn=%d",
                  config.sub_venc_channel);
            return false;
        }
    }

    (void)VencChannelControl::RequestIdr(impl_->main_venc_);
    if (need_sub_stream) {
        (void)VencChannelControl::RequestIdr(impl_->sub_venc_);
    }

    return true;
}

void UnbindVpssVencChannels(MppHisiSdkImpl& impl) {
    VencChannelControl::UnbindFromVpss(impl.sub_venc_);
    VencChannelControl::UnbindFromVpss(impl.main_venc_);
}

void MppHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
    (void)config;
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    StopVencStreamThread(*impl_);
    UnbindVpssVencChannels(*impl_);
}

// ====================================================================
// StartVencStream / StopVencStream
// ====================================================================
bool MppHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                 MediaFrameCallback callback,
                                 void* user) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (impl_->stream_running_.load() || impl_->stream_thread_.joinable()) {
        return true;
    }
    if (!impl_->main_venc_.receiving ||
        (config.sub_stream.enabled && !impl_->sub_venc_.receiving)) {
        Error("hisi_vendor", "start VENC stream before VENC recv is running");
        return false;
    }

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

    impl_->frame_callback_ = callback;
    impl_->frame_callback_user_ = user;
    impl_->stream_running_.store(true);

    // One stream capture thread monitors all enabled VENC channels.
    impl_->stream_thread_ = std::thread(
        VencStreamLoop, config, callback, user,
        std::ref(impl_->stream_running_));

    return true;
}

void MppHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
    (void)config;
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    StopVencStreamThread(*impl_);
}

// ====================================================================
// RequestIdr
// ====================================================================
bool MppHisiSdk::RequestIdr(int32_t venc_channel) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (venc_channel < 0) {
        return false;
    }
    VencChannelInfo* state = VencChannelControl::Find(
        impl_->main_venc_, impl_->sub_venc_, venc_channel);
    if (state == nullptr || !state->created) {
        return false;
    }
    return VencChannelControl::RequestIdr(*state);
}

bool MppHisiSdk::ApplyVencRoi(int32_t venc_channel,
                              const VideoStreamConfig& stream_config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    VencChannelInfo* state = VencChannelControl::Find(
        impl_->main_venc_, impl_->sub_venc_, venc_channel);
    if (state == nullptr || !state->created) {
        return false;
    }
    if (!ApplyVencRoiConfig(venc_channel, stream_config)) {
        return false;
    }
    state->stream_config.roi = stream_config.roi;
    return VencChannelControl::RequestIdr(*state);
}

}  // namespace hisisdk
}  // namespace live_stream
