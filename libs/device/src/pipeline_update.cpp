#include "pipeline_update.h"

#include "device_features.h"
#include "infra/log.h"
#include "media_channels.h"

namespace live_stream {
namespace device_internal {

PipelineUpdate::PipelineUpdate(MediaPipeline& pipeline,
                               DeviceFeatures& features)
    : pipeline_(pipeline), features_(features) {}

PipelineUpdateResult PipelineUpdate::Apply(
    const PipelineUpdateRequest& request) {
    PipelineUpdateResult result;
    if (request.is_started) {
        pipeline_.Stop();
    }

    bool deinit_ok = true;
    if (request.has_system) {
        deinit_ok = pipeline_.DeinitSystem();
    }
    if (deinit_ok) {
        result.applied = ApplyNext(request);
    }

    if (!result.applied && deinit_ok &&
        (request.is_started || request.has_system)) {
        result.restored = RestorePrev(request);
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

bool PipelineUpdate::ApplyNext(const PipelineUpdateRequest& request) {
    pipeline_.SetConfig(request.next_config);
    const MediaChannels next_channels =
        BuildChannelsForConfig(request.next_config);

    if (request.has_system && !pipeline_.InitSystem()) {
        return false;
    }
    if (!features_.Bind(next_channels)) {
        return false;
    }
    if (request.is_started && !pipeline_.Start()) {
        return false;
    }
    if (request.is_started && !ApplyImage(request.prev_image_config)) {
        return false;
    }
    if (request.is_started && !features_.Start()) {
        return false;
    }
    return true;
}

bool PipelineUpdate::RestorePrev(const PipelineUpdateRequest& request) {
    features_.Stop();
    pipeline_.Stop();
    if (request.has_system) {
        (void)pipeline_.DeinitSystem();
    }

    pipeline_.SetConfig(request.prev_config);
    const MediaChannels prev_channels =
        BuildChannelsForConfig(request.prev_config);

    bool restored = true;
    if (request.has_system && !pipeline_.InitSystem()) {
        restored = false;
    }
    if (restored && !features_.Bind(prev_channels)) {
        restored = false;
    }
    if (restored && request.is_started && !pipeline_.Start()) {
        restored = false;
    }
    if (restored && request.is_started &&
        !ApplyImage(request.prev_image_config)) {
        restored = false;
    }
    if (restored && request.is_started && !features_.Start()) {
        restored = false;
    }
    if (!restored) {
        features_.Stop();
        pipeline_.Stop();
        if (request.has_system) {
            (void)pipeline_.DeinitSystem();
        }
    }
    return restored;
}

bool PipelineUpdate::ApplyImage(const Json& image_config) {
    if (!image_config.is_object() || image_config.empty()) {
        return true;
    }
    return pipeline_.ApplyImageConfig(image_config);
}

}  // namespace device_internal
}  // namespace live_stream
