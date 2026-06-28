#include "hisi_mpp_venc_channel.h"

#include "hisi_mpp_sdk.h"
#include "hisi_mpp_venc_attrs.h"
#include "venc_config.h"

#include "infra/log.h"

#include <cstddef>
#include <cstdint>

namespace live_stream {
namespace hisisdk {
namespace venc_internal {
namespace {

constexpr uint32_t kMaxVencRoiRegions = 8;

bool StartRecvFrame(VENC_CHN venc) {
    VENC_RECV_PIC_PARAM_S recv_param{};
    recv_param.s32RecvPicNum = -1;
    const HI_S32 status = HI_MPI_VENC_StartRecvFrame(venc, &recv_param);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_StartRecvFrame chn=%d failed: 0x%08x", venc,
              status);
        return false;
    }
    return true;
}

void StopRecvFrame(VENC_CHN venc, int32_t chn) {
    const HI_S32 stop_status = HI_MPI_VENC_StopRecvFrame(venc);
    if (stop_status != HI_SUCCESS) {
        Warn("hisi_vendor",
             "HI_MPI_VENC_StopRecvFrame chn=%d failed: 0x%08x", chn,
             stop_status);
    }
}

void DestroyVencMpiChannel(VENC_CHN venc) {
    (void)HI_MPI_VENC_DestroyChn(venc);
}

bool CreateConfiguredVencChannel(int32_t chn,
                                 const VideoStreamConfig& stream) {
    if (!ValidateVencStreamConfig(chn, stream)) {
        return false;
    }

    VencChannelAttrs attrs;
    if (!attrs.Build(stream)) {
        return false;
    }
    const VENC_CHN venc = static_cast<VENC_CHN>(chn);
    const VENC_CHN_ATTR_S& attr = attrs.value();

    Info(
        "hisi_vendor",
        "Create VENC chn=%d codec=%s rc=%s gop_mode=%s size=%ux%u "
        "src_fps=%d dst_fps=%d bitrate=%u gop=%u stat_time=%u buf=%u",
        chn, CodecName(stream.codec), RcModeName(stream.rc_mode),
        GopModeName(stream.gop_mode), stream.size.width, stream.size.height,
        stream.frame_rate.source_fps, stream.frame_rate.target_fps,
        stream.bitrate_kbps, stream.gop, attrs.stat_time(),
        attr.stVencAttr.u32BufSize);
    const HI_S32 create_status = HI_MPI_VENC_CreateChn(venc, &attr);
    if (create_status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_CreateChn chn=%d codec=%s size=%ux%u failed: "
              "0x%08x",
              chn, CodecName(stream.codec), stream.size.width,
              stream.size.height, create_status);
        return false;
    }
    if (!TuneRcParam(venc, attr.stRcAttr.enRcMode)) {
        DestroyVencMpiChannel(venc);
        return false;
    }
    if (!ApplyVencRoiConfig(chn, stream)) {
        DestroyVencMpiChannel(venc);
        return false;
    }
    return true;
}

void FillRoiFrameAttr(const VideoRoiRegion& region,
                      VENC_ROI_ATTR_EX_S& roi_attr) {
    for (uint32_t frame_index = 0; frame_index < 3; ++frame_index) {
        roi_attr.bEnable[frame_index] = region.enabled ? HI_TRUE : HI_FALSE;
        roi_attr.bAbsQp[frame_index] =
            region.absolute_qp ? HI_TRUE : HI_FALSE;
        roi_attr.s32Qp[frame_index] = region.qp;
        roi_attr.stRect[frame_index].s32X = static_cast<HI_S32>(region.x);
        roi_attr.stRect[frame_index].s32Y = static_cast<HI_S32>(region.y);
        roi_attr.stRect[frame_index].u32Width = region.width;
        roi_attr.stRect[frame_index].u32Height = region.height;
    }
}

bool ApplyVencRoiSlot(VENC_CHN venc,
                      uint32_t index,
                      const VideoRoiRegion* region) {
    VENC_ROI_ATTR_EX_S roi_attr{};
    roi_attr.u32Index = index;
    if (region != nullptr) {
        FillRoiFrameAttr(*region, roi_attr);
    }
    const HI_S32 status = HI_MPI_VENC_SetRoiAttrEx(venc, &roi_attr);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_SetRoiAttrEx chn=%d index=%u failed: 0x%08x",
              venc, index, status);
        return false;
    }
    return true;
}

bool BindVpssToVenc(int32_t vpss_group,
                    int32_t vpss_channel,
                    int32_t venc_channel) {
    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = vpss_group;
    src.s32ChnId = vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = venc_channel;

    const HI_S32 status = HI_MPI_SYS_Bind(&src, &dst);
    if (status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "HI_MPI_SYS_Bind VPSS-VENC vpss=%d:%d venc=%d failed: 0x%08x",
            vpss_group, vpss_channel, venc_channel, status);
        return false;
    }
    return true;
}

void UnbindVpssFromVenc(int32_t vpss_group,
                        int32_t vpss_channel,
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

bool RoiRegionMatches(const VideoRoiRegion& left,
                      const VideoRoiRegion& right) {
    return left.enabled == right.enabled && left.x == right.x &&
           left.y == right.y && left.width == right.width &&
           left.height == right.height && left.qp == right.qp &&
           left.absolute_qp == right.absolute_qp;
}

bool RoiConfigMatches(const VideoRoiConfig& left,
                      const VideoRoiConfig& right) {
    if (left.enabled != right.enabled ||
        left.regions.size() != right.regions.size()) {
        return false;
    }
    for (size_t i = 0; i < left.regions.size(); ++i) {
        if (!RoiRegionMatches(left.regions[i], right.regions[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool ApplyVencRoiConfig(int32_t venc_channel,
                        const VideoStreamConfig& stream_config) {
    if (venc_channel < 0) {
        return false;
    }
    if (stream_config.roi.regions.size() > kMaxVencRoiRegions) {
        Error("hisi_vendor", "VENC ROI regions exceed limit chn=%d size=%zu",
              venc_channel, stream_config.roi.regions.size());
        return false;
    }
    if (stream_config.roi.enabled &&
        stream_config.codec != Codec::kH264 &&
        stream_config.codec != Codec::kH265) {
        Error("hisi_vendor", "VENC ROI unsupported codec chn=%d codec=%s",
              venc_channel, CodecName(stream_config.codec));
        return false;
    }

    const VENC_CHN venc = static_cast<VENC_CHN>(venc_channel);
    for (uint32_t index = 0; index < kMaxVencRoiRegions; ++index) {
        const VideoRoiRegion* region = nullptr;
        if (stream_config.roi.enabled &&
            index < stream_config.roi.regions.size()) {
            region = &stream_config.roi.regions[index];
        }
        if (!ApplyVencRoiSlot(venc, index, region)) {
            return false;
        }
    }
    return true;
}

bool VencChannelControl::IsCreated(const VencChannelInfo& channel) {
    return channel.created;
}

bool VencChannelControl::IsBoundToVpss(const VencChannelInfo& channel) {
    return channel.bound_to_vpss;
}

bool VencChannelControl::Matches(const VencChannelInfo& channel,
                                 int32_t venc_channel,
                                 int32_t vpss_group,
                                 int32_t vpss_channel,
                                 const VideoStreamConfig& stream) {
    return channel.created && channel.venc_channel == venc_channel &&
           channel.vpss_group == vpss_group &&
           channel.vpss_channel == vpss_channel &&
           channel.stream_config.stream_id == stream.stream_id &&
           channel.stream_config.enabled == stream.enabled &&
           channel.stream_config.codec == stream.codec &&
           channel.stream_config.size.width == stream.size.width &&
           channel.stream_config.size.height == stream.size.height &&
           channel.stream_config.frame_rate.source_fps ==
               stream.frame_rate.source_fps &&
           channel.stream_config.frame_rate.target_fps ==
               stream.frame_rate.target_fps &&
           channel.stream_config.bitrate_kbps == stream.bitrate_kbps &&
           channel.stream_config.gop == stream.gop &&
           channel.stream_config.rc_mode == stream.rc_mode &&
           channel.stream_config.gop_mode == stream.gop_mode &&
           RoiConfigMatches(channel.stream_config.roi, stream.roi);
}

void VencChannelControl::Reset(VencChannelInfo& channel) {
    channel = VencChannelInfo{};
}

void VencChannelControl::Init(VencChannelInfo& channel,
                              StreamId stream_id,
                              int32_t venc_channel,
                              int32_t vpss_group,
                              int32_t vpss_channel,
                              Codec codec) {
    channel.stream_id = stream_id;
    channel.venc_channel = venc_channel;
    channel.vpss_group = vpss_group;
    channel.vpss_channel = vpss_channel;
    channel.codec = codec;
    channel.created = false;
    channel.bound_to_vpss = false;
    channel.receiving = false;
    channel.fd = -1;
}

bool VencChannelControl::Create(VencChannelInfo& channel,
                                const VideoStreamConfig& stream) {
    if (channel.created) {
        return true;
    }
    if (!CreateConfiguredVencChannel(channel.venc_channel, stream)) {
        return false;
    }
    channel.created = true;
    channel.codec = stream.codec;
    channel.stream_config = stream;
    channel.fd = HI_MPI_VENC_GetFd(static_cast<VENC_CHN>(
        channel.venc_channel));
    return true;
}

void VencChannelControl::StopRecv(VencChannelInfo& channel) {
    if (!channel.receiving) {
        return;
    }
    StopRecvFrame(static_cast<VENC_CHN>(channel.venc_channel),
                  channel.venc_channel);
    channel.receiving = false;
}

void VencChannelControl::UnbindFromVpss(VencChannelInfo& channel) {
    if (!channel.bound_to_vpss) {
        return;
    }
    StopRecv(channel);
    UnbindVpssFromVenc(channel.vpss_group, channel.vpss_channel,
                       channel.venc_channel);
    channel.bound_to_vpss = false;
}

void VencChannelControl::Destroy(VencChannelInfo& channel) {
    if (!channel.created) {
        Reset(channel);
        return;
    }
    StopRecv(channel);
    UnbindFromVpss(channel);
    DestroyVencMpiChannel(static_cast<VENC_CHN>(channel.venc_channel));
    Reset(channel);
}

bool VencChannelControl::BindToVpss(VencChannelInfo& channel) {
    if (!channel.created) {
        return false;
    }
    if (channel.bound_to_vpss) {
        return true;
    }
    if (!BindVpssToVenc(channel.vpss_group, channel.vpss_channel,
                        channel.venc_channel)) {
        return false;
    }
    channel.bound_to_vpss = true;
    return true;
}

bool VencChannelControl::StartRecv(VencChannelInfo& channel) {
    if (!channel.created || !channel.bound_to_vpss) {
        return false;
    }
    if (channel.receiving) {
        return true;
    }
    if (!StartRecvFrame(static_cast<VENC_CHN>(channel.venc_channel))) {
        return false;
    }
    channel.receiving = true;
    return true;
}

bool VencChannelControl::RequestIdr(const VencChannelInfo& channel) {
    if (!channel.created || !IsIdrCodec(channel.codec)) {
        return false;
    }
    const HI_S32 status = HI_MPI_VENC_RequestIDR(
        static_cast<VENC_CHN>(channel.venc_channel), HI_TRUE);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_VENC_RequestIDR chn=%d codec=%s failed: 0x%08x",
              channel.venc_channel, CodecName(channel.codec), status);
        return false;
    }
    return true;
}

VencChannelInfo* VencChannelControl::Find(VencChannelInfo& main_channel,
                                          VencChannelInfo& sub_channel,
                                          int32_t venc_channel) {
    if (main_channel.created && main_channel.venc_channel == venc_channel) {
        return &main_channel;
    }
    if (sub_channel.created && sub_channel.venc_channel == venc_channel) {
        return &sub_channel;
    }
    return nullptr;
}

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream
