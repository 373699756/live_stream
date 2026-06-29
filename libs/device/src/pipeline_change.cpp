#include "pipeline_change.h"

#include "device_features.h"
#include "infra/log.h"
#include "media_channels.h"

namespace live_stream {
namespace device_internal {

PipelineChange::PipelineChange(MediaPipeline& pipeline,
                               DeviceFeatures& features)
    : pipeline_(pipeline), features_(features) {}

PipelineChangeInfo PipelineChange::Apply(
    const PipelineChangePlan& plan) {
    PipelineChangeInfo result;
    if (plan.is_started) {
        pipeline_.Stop();
    }

    bool deinit_ok = true;
    if (plan.system_initialized) {
        deinit_ok = pipeline_.DeinitSystem();
    }
    if (deinit_ok) {
        result.applied = ApplyNextConfig(plan);
    }

    if (!result.applied && deinit_ok &&
        (plan.is_started || plan.system_initialized)) {
        result.restored = RestorePrevConfig(plan);
        if (!result.restored) {
            Error("device",
                  "restore media pipeline after config failure failed");
        } else {
            Error("device",
                  "media config apply failed, restored previous pipeline");
        }
    }
    return result;
}

bool PipelineChange::ApplyNextConfig(const PipelineChangePlan& plan) {
    pipeline_.SetConfig(plan.next_config);
    const MediaChannels next_channels =
        BuildChannelsForConfig(plan.next_config);

    if (plan.system_initialized && !pipeline_.InitSystem()) {
        return false;
    }
    if (!features_.Bind(next_channels)) {
        return false;
    }
    if (plan.is_started && !pipeline_.Start()) {
        return false;
    }
    if (plan.is_started && !ApplyImageConfig(plan.prev_image_config)) {
        return false;
    }
    if (plan.is_started && !features_.Start()) {
        return false;
    }
    return true;
}

bool PipelineChange::RestorePrevConfig(const PipelineChangePlan& plan) {
    features_.Stop();
    pipeline_.Stop();
    if (plan.system_initialized) {
        (void)pipeline_.DeinitSystem();
    }

    pipeline_.SetConfig(plan.prev_config);
    const MediaChannels prev_channels =
        BuildChannelsForConfig(plan.prev_config);

    bool restored = true;
    if (plan.system_initialized && !pipeline_.InitSystem()) {
        restored = false;
    }
    if (restored && !features_.Bind(prev_channels)) {
        restored = false;
    }
    if (restored && plan.is_started && !pipeline_.Start()) {
        restored = false;
    }
    if (restored && plan.is_started &&
        !ApplyImageConfig(plan.prev_image_config)) {
        restored = false;
    }
    if (restored && plan.is_started && !features_.Start()) {
        restored = false;
    }
    if (!restored) {
        features_.Stop();
        pipeline_.Stop();
        if (plan.system_initialized) {
            (void)pipeline_.DeinitSystem();
        }
    }
    return restored;
}

bool PipelineChange::ApplyImageConfig(const Json& image_config) {
    if (!image_config.is_object() || image_config.empty()) {
        return true;
    }
    return pipeline_.ApplyImageConfig(image_config);
}

}  // namespace device_internal
}  // namespace live_stream
