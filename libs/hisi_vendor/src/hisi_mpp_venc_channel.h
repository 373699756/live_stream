#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_CHANNEL_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_CHANNEL_H_

#include "mpp_hisi_sdk_impl.h"

namespace live_stream {
namespace hisisdk {
namespace venc_internal {

class VencChannelControl {
public:
    static bool IsCreated(const VencChannelInfo& channel);
    static bool IsBoundToVpss(const VencChannelInfo& channel);
    static bool Matches(const VencChannelInfo& channel,
                        int32_t venc_channel,
                        int32_t vpss_group,
                        int32_t vpss_channel,
                        const VideoStreamConfig& stream);
    static void Reset(VencChannelInfo& channel);
    static void Init(VencChannelInfo& channel,
                     StreamId stream_id,
                     int32_t venc_channel,
                     int32_t vpss_group,
                     int32_t vpss_channel,
                     Codec codec);
    static bool Create(VencChannelInfo& channel,
                       const VideoStreamConfig& stream);
    static void Destroy(VencChannelInfo& channel);
    static bool BindToVpss(VencChannelInfo& channel);
    static void UnbindFromVpss(VencChannelInfo& channel);
    static bool StartRecv(VencChannelInfo& channel);
    static void StopRecv(VencChannelInfo& channel);
    static bool RequestIdr(const VencChannelInfo& channel);
    static VencChannelInfo* Find(VencChannelInfo& main_channel,
                                 VencChannelInfo& sub_channel,
                                 int32_t venc_channel);
};

bool ApplyVencRoiConfig(int32_t venc_channel,
                        const VideoStreamConfig& stream_config);

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_CHANNEL_H_
