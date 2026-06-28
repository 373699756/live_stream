#ifndef LIVE_STREAM_APP_CONFIG_PROTOCOL_CONFIG_CHANGE_H_
#define LIVE_STREAM_APP_CONFIG_PROTOCOL_CONFIG_CHANGE_H_

#include "config/app_config.h"

namespace live_stream {

ConfigCode VerifyProtocolConfigChangeScope(
    const AppConfig &current_config,
    const AppConfig &next_config,
    const std::string &scope,
    ConfigError *error);

bool IsRtspConfigChanged(const AppConfig &current_config,
                         const AppConfig &next_config);
bool IsWebrtcConfigChanged(const AppConfig &current_config,
                           const AppConfig &next_config);
bool IsOnvifConfigChanged(const AppConfig &current_config,
                          const AppConfig &next_config);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_CONFIG_PROTOCOL_CONFIG_CHANGE_H_
