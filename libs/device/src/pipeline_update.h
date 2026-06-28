#ifndef LIVE_STREAM_DEVICE_SRC_PIPELINE_UPDATE_H_
#define LIVE_STREAM_DEVICE_SRC_PIPELINE_UPDATE_H_

#include "device.h"
#include "media_pipeline.h"

namespace live_stream {
namespace device_internal {

class DeviceFeatures;

struct PipelineUpdateRequest {
    MediaPipelineConfig next_config;
    MediaPipelineConfig prev_config;
    Json prev_image_config;
    bool is_started = false;
    bool has_system = false;
};

struct PipelineUpdateResult {
    bool applied = false;
    bool restored = false;
};

class PipelineUpdate {
public:
    PipelineUpdate(MediaPipeline& pipeline, DeviceFeatures& features);

    PipelineUpdateResult Apply(const PipelineUpdateRequest& request);

private:
    bool ApplyNext(const PipelineUpdateRequest& request);
    bool RestorePrev(const PipelineUpdateRequest& request);
    bool ApplyImage(const Json& image_config);

    MediaPipeline& pipeline_;
    DeviceFeatures& features_;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_PIPELINE_UPDATE_H_
