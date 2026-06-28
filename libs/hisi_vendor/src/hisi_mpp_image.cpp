#include "hisi_vendor/mpp_sdk.h"
#include "hisi_mpp_image_controls.h"
#include "hisi_mpp_sdk.h"
#include "mpp_hisi_sdk_impl.h"

#include "infra/log.h"

#include <mutex>

namespace live_stream {
namespace hisisdk {

bool MppHisiSdk::ApplyImageConfig(const MediaPipelineConfig& config,
                                  const Json& image_config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (!image_config.is_object()) {
        return false;
    }

    IspImageControls isp_controls(config);
    if (!isp_controls.Apply(image_config)) {
        return false;
    }

    ImageTransformControls transform_controls(*impl_, config);
    return transform_controls.Apply(image_config);
}

ExposureInfo MppHisiSdk::QueryExposureInfo(
    const MediaPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    ExposureInfo info;
    if (!impl_->isp_started_) {
        return info;
    }

    ISP_EXP_INFO_S exp_info{};
    const VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
    const HI_S32 status = HI_MPI_ISP_QueryExposureInfo(vi_pipe, &exp_info);
    if (status != HI_SUCCESS) {
        Error("hisi_vendor",
              "HI_MPI_ISP_QueryExposureInfo pipe=%d failed: 0x%08x",
              vi_pipe, status);
        return info;
    }

    info.valid = true;
    info.exposure_time_us = exp_info.u32ExpTime;
    info.analog_gain = exp_info.u32AGain;
    info.digital_gain = exp_info.u32DGain;
    info.isp_digital_gain = exp_info.u32ISPDGain;
    info.iso = exp_info.u32ISO;
    return info;
}

}  // namespace hisisdk
}  // namespace live_stream
