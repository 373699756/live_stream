#ifndef LIVE_STREAM_NETWORK_SERVICE_SRC_NETWORK_CONFIG_CODEC_H_
#define LIVE_STREAM_NETWORK_SERVICE_SRC_NETWORK_CONFIG_CODEC_H_

#include "network_service.h"

#include <map>
#include <string>

namespace live_stream {
namespace network_internal {

bool IsValidIfname(const std::string &ifname);
bool ValidateConfig(const NetworkInterfaceConfig &config,
                    bool allow_loopback_config);
NetworkInterfaceConfig DefaultConfig(const std::string &ifname);
bool ConfigsFromNetworkJson(
    const ConfigJson &json,
    std::map<std::string, NetworkInterfaceConfig> *configs);
ConfigJson NetworkJsonWithConfigs(
    const ConfigJson &current,
    const std::map<std::string, NetworkInterfaceConfig> &configs);
bool ConfigFromNetworkInterfaceJson(const std::string &ifname,
                                    const ConfigJson &value,
                                    NetworkInterfaceConfig *config);

}  // namespace network_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NETWORK_SERVICE_SRC_NETWORK_CONFIG_CODEC_H_
