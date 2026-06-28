#include "config_scopes.h"

namespace live_stream {
namespace device_internal {

bool ConfigScopes::Attach(IConfig* config,
                          const ConfigScope& video_scope,
                          const ConfigScope& image_scope,
                          AttachedConfigs& attached_now) {
    attached_now = AttachedConfigs{};
    if (config == nullptr) {
        return true;
    }

    if (!video_attached_) {
        if (!config->AddScope("video", video_scope)) {
            return false;
        }
        video_attached_ = true;
        attached_now.video = true;
    }

    if (!image_attached_) {
        if (!config->AddScope("image", image_scope)) {
            if (attached_now.video) {
                (void)config->RemoveScope("video");
                video_attached_ = false;
            }
            attached_now = AttachedConfigs{};
            return false;
        }
        image_attached_ = true;
        attached_now.image = true;
    }
    return true;
}

void ConfigScopes::Detach(IConfig* config) {
    const bool detach_video = video_attached_;
    const bool detach_image = image_attached_;
    video_attached_ = false;
    image_attached_ = false;

    if (config == nullptr) {
        return;
    }
    if (detach_video) {
        (void)config->RemoveScope("video");
    }
    if (detach_image) {
        (void)config->RemoveScope("image");
    }
}

}  // namespace device_internal
}  // namespace live_stream
