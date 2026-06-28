#ifndef LIVE_STREAM_DEVICE_SRC_CONFIG_SCOPES_H_
#define LIVE_STREAM_DEVICE_SRC_CONFIG_SCOPES_H_

#include "config.h"

namespace live_stream {
namespace device_internal {

struct AttachedConfigs {
    bool video = false;
    bool image = false;
};

class ConfigScopes {
public:
    bool Attach(IConfig* config,
                const ConfigScope& video_scope,
                const ConfigScope& image_scope,
                AttachedConfigs& attached_configs);
    void Detach(IConfig* config);

private:
    bool video_attached_ = false;
    bool image_attached_ = false;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_CONFIG_SCOPES_H_
