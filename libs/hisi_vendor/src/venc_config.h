#ifndef LIVE_STREAM_HISI_VENDOR_SRC_VENC_CONFIG_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_VENC_CONFIG_H_

#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"

namespace live_stream {
namespace hisisdk {
namespace venc_internal {

PAYLOAD_TYPE_E PayloadFromCodec(VideoCodec codec);
VENC_RC_MODE_E RcModeFromConfig(VideoCodec codec, RateControlMode mode);
VENC_GOP_ATTR_S GopAttrFromConfig(GopMode mode, uint32_t gop);
uint32_t StatTimeFromConfig(const VENC_GOP_ATTR_S &gop_attr, uint32_t gop);
const char *CodecName(VideoCodec codec);
const char *RcModeName(RateControlMode mode);
const char *GopModeName(GopMode mode);
bool IsIdrCodec(VideoCodec codec);
bool ValidateVencStreamConfig(int32_t chn, const VideoStreamConfig &stream);
bool TuneRcParam(VENC_CHN venc, VENC_RC_MODE_E rc_mode);

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_VENC_CONFIG_H_
