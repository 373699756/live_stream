#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_IMAGE_CONTROLS_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_IMAGE_CONTROLS_H_

#include "hisi_vendor/media_pipeline.h"
#include "hisi_mpp_sdk.h"
#include "json.h"

namespace live_stream {
namespace hisisdk {

struct MppHisiSdkImpl;

class IspImageControls {
public:
    explicit IspImageControls(const MediaPipelineConfig& config);

    bool Apply(const Json& image_config);

private:
    bool ApplyBasic(const Json& basic);
    bool ApplyExposure(const Json& exposure);
    bool ApplyWhiteBalance(const Json& white_balance);
    bool ApplyEnhancement(const Json& enhancement);
    bool ApplyBacklight(const Json& backlight);
    bool ApplyOrientation(const Json& orientation);
    bool ApplyColorMode(const Json& image_config);

    VI_PIPE vi_pipe_;
    VI_CHN vi_channel_;
};

class ImageTransformControls {
public:
    ImageTransformControls(MppHisiSdkImpl& impl,
                           const MediaPipelineConfig& config);

    bool Apply(const Json& image_config);

private:
    MppHisiSdkImpl& impl_;
    const MediaPipelineConfig& config_;
};

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_IMAGE_CONTROLS_H_
