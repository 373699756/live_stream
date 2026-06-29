#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_ATTRS_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_ATTRS_H_

#include "hisi_mpp_sdk.h"
#include "hisi_vendor/mpp_sdk.h"

namespace live_stream {
namespace hisisdk {
namespace venc_internal {

class VencChannelAttrs {
public:
    bool Build(const VideoStreamConfig& stream);

    const VENC_CHN_ATTR_S& value() const { return attr_; }
    uint32_t stat_time() const { return stat_time_; }

private:
    void FillCommonAttrs(const VideoStreamConfig& stream);
    void FillH264RcAttrs(const VideoStreamConfig& stream);
    void FillH265RcAttrs(const VideoStreamConfig& stream);
    void FillMjpegRcAttrs(const VideoStreamConfig& stream);

    VENC_CHN_ATTR_S attr_{};
    uint32_t stat_time_ = 0;
};

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_ATTRS_H_
