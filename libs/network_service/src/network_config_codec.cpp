#include "network_config_codec.h"

#include "live_stream/json_utils.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace network_internal {
namespace {

constexpr std::size_t kMaxIfnameLength = 15;
constexpr std::size_t kMaxDnsServers = 4;

bool IsValidIpv4(const std::string &value) {
  if (value.empty()) {
    return false;
  }
  int octets = 0;
  std::size_t start = 0;
  while (start < value.size()) {
    if (octets >= 4) {
      return false;
    }
    std::size_t end = value.find('.', start);
    if (end == std::string::npos) {
      end = value.size();
    }
    if (end == start || end - start > 3) {
      return false;
    }
    int octet = 0;
    for (std::size_t i = start; i < end; ++i) {
      if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
        return false;
      }
      octet = octet * 10 + value[i] - '0';
    }
    if (octet > 255) {
      return false;
    }
    ++octets;
    start = end + 1;
  }
  return octets == 4 && value[value.size() - 1] != '.';
}

bool Ipv4ToUint32(const std::string &value, uint32_t *parsed) {
  if (parsed == nullptr || !IsValidIpv4(value)) {
    return false;
  }
  uint32_t result = 0;
  std::size_t start = 0;
  while (start < value.size()) {
    std::size_t end = value.find('.', start);
    if (end == std::string::npos) {
      end = value.size();
    }
    int octet = 0;
    for (std::size_t i = start; i < end; ++i) {
      octet = octet * 10 + value[i] - '0';
    }
    result = (result << 8) | static_cast<uint32_t>(octet);
    start = end + 1;
  }
  *parsed = result;
  return true;
}

bool PrefixFromNetmask(const std::string &netmask, uint8_t *prefix_length) {
  uint32_t mask = 0;
  if (prefix_length == nullptr || !Ipv4ToUint32(netmask, &mask)) {
    return false;
  }
  uint8_t prefix = 0;
  bool saw_zero = false;
  for (int bit = 31; bit >= 0; --bit) {
    const bool set = (mask & (static_cast<uint32_t>(1) << bit)) != 0;
    if (set && saw_zero) {
      return false;
    }
    if (set) {
      ++prefix;
    } else {
      saw_zero = true;
    }
  }
  if (prefix == 0 || prefix > 32) {
    return false;
  }
  *prefix_length = prefix;
  return true;
}

std::string NetmaskFromPrefix(uint8_t prefix_length) {
  if (prefix_length == 0 || prefix_length > 32) {
    return std::string();
  }
  const uint32_t mask =
      prefix_length == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix_length));
  return std::to_string((mask >> 24) & 0xff) + "." +
         std::to_string((mask >> 16) & 0xff) + "." +
         std::to_string((mask >> 8) & 0xff) + "." + std::to_string(mask & 0xff);
}

bool IsValidDnsServers(const std::vector<std::string> &dns_servers) {
  if (dns_servers.size() > kMaxDnsServers) {
    return false;
  }
  for (const std::string &dns : dns_servers) {
    if (!IsValidIpv4(dns)) {
      return false;
    }
  }
  return true;
}

bool ParseMode(const std::string &mode, NetworkAddressMode *address_mode) {
  if (address_mode == nullptr) {
    return false;
  }
  if (mode == "dhcp") {
    *address_mode = NetworkAddressMode::kDhcp;
    return true;
  }
  if (mode == "static") {
    *address_mode = NetworkAddressMode::kStatic;
    return true;
  }
  return false;
}

bool ConfigFromJson(const ConfigJson &value, NetworkInterfaceConfig *config) {
  if (!value.is_object() || config == nullptr) {
    return false;
  }
  NetworkInterfaceConfig parsed;
  std::string mode;
  if (!json_utils::Load(value, "ifname", &parsed.ifname) ||
      !json_utils::Load(value, "enabled", &parsed.enabled) ||
      !json_utils::Load(value, "address_mode", &mode) ||
      !ParseMode(mode, &parsed.address_mode) ||
      !json_utils::Load(value, "ipv4_address", &parsed.ipv4_address) ||
      !json_utils::Load(value, "prefix_length", &parsed.prefix_length, 0, 32) ||
      !json_utils::Load(value, "gateway", &parsed.gateway) ||
      !json_utils::LoadStringArray(value, "dns_servers", &parsed.dns_servers)) {
    return false;
  }
  *config = parsed;
  return true;
}

ConfigJson ConfigToNetworkInterfaceJson(const NetworkInterfaceConfig &config) {
  ConfigJson value = ConfigJson::object();
  value["enabled"] = config.enabled;
  value["dhcp"] = config.address_mode == NetworkAddressMode::kDhcp;
  ConfigJson static_ipv4 = ConfigJson::object();
  static_ipv4["address"] = config.ipv4_address;
  static_ipv4["netmask"] = NetmaskFromPrefix(config.prefix_length);
  static_ipv4["gateway"] = config.gateway;
  value["static_ipv4"] = static_ipv4;
  ConfigJson dns = ConfigJson::array();
  for (const std::string &server : config.dns_servers) {
    dns.push_back(server);
  }
  value["dns"] = dns;
  return value;
}

}  // namespace

bool IsValidIfname(const std::string &ifname) {
  if (ifname.empty() || ifname.size() > kMaxIfnameLength) {
    return false;
  }
  for (char c : ifname) {
    const unsigned char ch = static_cast<unsigned char>(c);
    if (!std::isalnum(ch) && c != '_' && c != '-' && c != '.' && c != ':') {
      return false;
    }
  }
  return true;
}

bool ValidateConfig(const NetworkInterfaceConfig &config,
                    bool allow_loopback_config) {
  if (!IsValidIfname(config.ifname)) {
    return false;
  }
  if (!allow_loopback_config && config.ifname == "lo") {
    return false;
  }
  if (!IsValidDnsServers(config.dns_servers)) {
    return false;
  }
  if (!config.gateway.empty() && !IsValidIpv4(config.gateway)) {
    return false;
  }
  switch (config.address_mode) {
  case NetworkAddressMode::kDhcp:
    if (config.prefix_length > 32) {
      return false;
    }
    break;
  case NetworkAddressMode::kStatic:
    if (!IsValidIpv4(config.ipv4_address) || config.prefix_length == 0 ||
        config.prefix_length > 32) {
      return false;
    }
    break;
  default:
    return false;
  }
  return true;
}

NetworkInterfaceConfig DefaultConfig(const std::string &ifname) {
  NetworkInterfaceConfig config;
  config.ifname = ifname.empty() ? "eth0" : ifname;
  config.enabled = true;
  config.address_mode = NetworkAddressMode::kDhcp;
  config.prefix_length = 24;
  return config;
}

bool ConfigFromNetworkInterfaceJson(const std::string &ifname,
                                    const ConfigJson &value,
                                    NetworkInterfaceConfig *config) {
  if (!value.is_object() || config == nullptr) {
    return false;
  }
  NetworkInterfaceConfig parsed;
  parsed.ifname = ifname;
  if (!json_utils::Load(value, "enabled", &parsed.enabled)) {
    return false;
  }
  bool dhcp = false;
  if (!json_utils::Load(value, "dhcp", &dhcp)) {
    return false;
  }
  parsed.address_mode =
      dhcp ? NetworkAddressMode::kDhcp : NetworkAddressMode::kStatic;
  if (!json_utils::LoadStringArray(value, "dns", &parsed.dns_servers)) {
    return false;
  }
  const ConfigJson *static_ipv4 = nullptr;
  if (!json_utils::LoadObject(value, "static_ipv4", &static_ipv4)) {
    return false;
  }
  std::string netmask;
  if (!json_utils::Load(*static_ipv4, "address", &parsed.ipv4_address) ||
      !json_utils::Load(*static_ipv4, "gateway", &parsed.gateway) ||
      !json_utils::Load(*static_ipv4, "netmask", &netmask) ||
      !PrefixFromNetmask(netmask, &parsed.prefix_length)) {
    return false;
  }
  *config = parsed;
  return true;
}

bool ConfigsFromNetworkJson(
    const ConfigJson &json,
    std::map<std::string, NetworkInterfaceConfig> *configs) {
  if (configs == nullptr) {
    return false;
  }
  configs->clear();
  if (json.is_array()) {
    for (const ConfigJson &item : json) {
      NetworkInterfaceConfig config;
      if (!ConfigFromJson(item, &config) || !ValidateConfig(config, true)) {
        return false;
      }
      (*configs)[config.ifname] = config;
    }
    return true;
  }
  if (!json.is_object()) {
    return false;
  }
  const ConfigJson *interfaces = nullptr;
  if (!json_utils::LoadObject(json, "interfaces", &interfaces)) {
    return false;
  }
  for (auto iter = interfaces->begin(); iter != interfaces->end(); ++iter) {
    NetworkInterfaceConfig config;
    if (!ConfigFromNetworkInterfaceJson(iter.key(), iter.value(), &config) ||
        !ValidateConfig(config, true)) {
      return false;
    }
    (*configs)[config.ifname] = config;
  }
  return true;
}

ConfigJson NetworkJsonWithConfigs(
    const ConfigJson &current,
    const std::map<std::string, NetworkInterfaceConfig> &configs) {
  ConfigJson root = current.is_object() ? current : ConfigJson::object();
  ConfigJson interfaces = ConfigJson::object();
  for (const auto &entry : configs) {
    interfaces[entry.first] = ConfigToNetworkInterfaceJson(entry.second);
  }
  root["interfaces"] = interfaces;
  return root;
}

}  // namespace network_internal

ConfigJson NetworkInterfaceStatusToApiJson(
    const NetworkInterfaceStatus &status) {
  ConfigJson root = ConfigJson::object();
  root["ifname"] = status.ifname;
  root["enabled"] = status.enabled;
  root["link_up"] = status.link_up;
  root["dhcp"] = status.dhcp_enabled;
  root["mac_address"] = status.mac_address;
  root["gateway"] = status.gateway;
  root["last_ok"] = status.last_ok;

  ConfigJson static_ipv4 = ConfigJson::object();
  static_ipv4["address"] = status.ipv4_address;
  static_ipv4["prefix_length"] = status.prefix_length;
  static_ipv4["netmask"] =
      network_internal::NetmaskFromPrefix(status.prefix_length);
  static_ipv4["gateway"] = status.gateway;
  root["static_ipv4"] = static_ipv4;

  ConfigJson dns = ConfigJson::array();
  for (const std::string &server : status.dns_servers) {
    dns.push_back(server);
  }
  root["dns"] = dns;
  return root;
}

bool NetworkInterfaceConfigFromApiJson(const std::string &ifname,
                                       const ConfigJson &value,
                                       NetworkInterfaceConfig *config) {
  return network_internal::ConfigFromNetworkInterfaceJson(ifname, value,
                                                          config);
}

}  // namespace live_stream
