#ifndef LIVE_STREAM_DEVICE_SRC_PIPELINE_CHANGE_H_
#define LIVE_STREAM_DEVICE_SRC_PIPELINE_CHANGE_H_

#include "device.h"
#include "media_pipeline.h"

namespace live_stream {
namespace device_internal {

class DeviceFeatures;

struct PipelineChangePlan {
    MediaPipelineConfig next_config;
    MediaPipelineConfig prev_config;
    Json prev_image_config;
    bool is_started = false;
    bool system_initialized = false;
};

struct PipelineChangeInfo {
    bool applied = false;
    bool restored = false;
};

class PipelineChange {
public:
    PipelineChange(MediaPipeline& pipeline, DeviceFeatures& features);

    PipelineChangeInfo Apply(const PipelineChangePlan& plan);

private:
    bool ApplyNextConfig(const PipelineChangePlan& plan);
    bool RestorePrevConfig(const PipelineChangePlan& plan);
    bool ApplyImageConfig(const Json& image_config);

    MediaPipeline& pipeline_;
    DeviceFeatures& features_;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_PIPELINE_CHANGE_H_
