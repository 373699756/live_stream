#ifndef LIVE_STREAM_APP_PROTOCOL_RUNTIME_CONFIG_H_
#define LIVE_STREAM_APP_PROTOCOL_RUNTIME_CONFIG_H_

#include "runtime_config.h"

namespace live_stream {

ConfigResult ValidateRuntimeConfigScope(
    const AppRuntimeConfig &current_config,
    const AppRuntimeConfig &next_config,
    const std::string &scope);

bool IsRtspRuntimeChanged(const AppRuntimeConfig &current_config,
                          const AppRuntimeConfig &next_config);
bool IsWebrtcRuntimeChanged(const AppRuntimeConfig &current_config,
                            const AppRuntimeConfig &next_config);
bool IsOnvifRuntimeChanged(const AppRuntimeConfig &current_config,
                           const AppRuntimeConfig &next_config);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PROTOCOL_RUNTIME_CONFIG_H_
