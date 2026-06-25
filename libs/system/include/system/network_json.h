#ifndef LIVE_STREAM_SYSTEM_NETWORK_JSON_H_
#define LIVE_STREAM_SYSTEM_NETWORK_JSON_H_

#include "json.h"
#include "system/network.h"

#include <cstdint>
#include <string>

namespace live_stream {

Json NetInterfaceInfoToApiJson(const NetInterfaceInfo& interface_info);
bool NetConfigFromApiJson(const std::string& ifname,
                          const Json& value,
                          NetConfig* config);

bool NetmaskToPrefixLength(const std::string& netmask,
                           uint8_t* prefix_length);
std::string PrefixLengthToNetmask(uint8_t prefix_length);

}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_NETWORK_JSON_H_
