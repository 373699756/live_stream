#ifndef LIVE_STREAM_SYSTEM_SRC_NETWORK_JSON_H_
#define LIVE_STREAM_SYSTEM_SRC_NETWORK_JSON_H_

#include "json.h"
#include "system/network.h"

#include <map>
#include <string>

namespace live_stream {
namespace network_internal {

bool IsValidIfname(const std::string &ifname);
bool ValidateConfig(const NetConfig &config,
                    bool allow_loopback_config);
NetConfig DefaultConfig(const std::string &ifname);
bool ConfigFromNetJson(const std::string &ifname,
                       const Json &value,
                       NetConfig *config);
Json NetConfigToJson(const NetConfig &config);
bool ConfigsFromNetworkJson(
    const Json &json,
    std::map<std::string, NetConfig> *configs);
Json NetworkJsonWithConfigs(
    const Json &current,
    const std::map<std::string, NetConfig> &configs);

}  // namespace network_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_SRC_NETWORK_JSON_H_
