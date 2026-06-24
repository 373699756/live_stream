#ifndef LIVE_STREAM_SYSTEM_NETWORK_FORMAT_H_
#define LIVE_STREAM_SYSTEM_NETWORK_FORMAT_H_

#include "config_json.h"
#include "network_api.h"

#include <cstdint>
#include <string>

namespace live_stream {

ConfigJson NetInterfaceInfoToApiJson(const NetInterfaceInfo& status);
bool NetConfigFromApiJson(const std::string& ifname,
                          const ConfigJson& value,
                          NetConfig* config);

bool NetmaskToPrefixLength(const std::string& netmask,
                           uint8_t* prefix_length);
std::string PrefixLengthToNetmask(uint8_t prefix_length);

}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_NETWORK_FORMAT_H_
