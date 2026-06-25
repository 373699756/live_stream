#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VB_CONFIG_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VB_CONFIG_H_

#include "hisi_vendor/media_pipeline.h"

namespace live_stream {
namespace hisisdk {
namespace mpp_vb_config {

bool ConfigureFrameBuffer(const MediaPipelineConfig& config,
                          bool* cleanup_failed);

}  // namespace mpp_vb_config
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VB_CONFIG_H_
