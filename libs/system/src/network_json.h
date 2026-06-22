#ifndef LIVE_STREAM_SYSTEM_SRC_NETWORK_JSON_H_
#define LIVE_STREAM_SYSTEM_SRC_NETWORK_JSON_H_

#include "config_json.h"
#include "network_api.h"

#include <map>
#include <string>

namespace live_stream {
namespace network_internal {

bool IsValidIfname(const std::string &ifname);
bool ValidateConfig(const NetConfig &config,
                    bool allow_loopback_config);
NetConfig DefaultConfig(const std::string &ifname);
bool ConfigFromNetJson(const std::string &ifname,
                       const ConfigJson &value,
                       NetConfig *config);
ConfigJson NetConfigToJson(const NetConfig &config);
bool ConfigsFromNetworkJson(
    const ConfigJson &json,
    std::map<std::string, NetConfig> *configs);
ConfigJson NetworkJsonWithConfigs(
    const ConfigJson &current,
    const std::map<std::string, NetConfig> &configs);

}  // namespace network_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_SRC_NETWORK_JSON_H_
