#include "network_json.h"

#include "json_reader.h"
#include "system/network_json.h"

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

bool IsValidNetmask(const std::string &netmask) {
    if (!IsValidIpv4(netmask)) {
        return false;
    }
    // Parse to uint32 and verify it is a contiguous prefix mask.
    uint32_t result = 0;
    std::size_t start = 0;
    while (start < netmask.size()) {
        std::size_t end = netmask.find('.', start);
        if (end == std::string::npos) {
            end = netmask.size();
        }
        int octet = 0;
        for (std::size_t i = start; i < end; ++i) {
            octet = octet * 10 + netmask[i] - '0';
        }
        result = (result << 8) | static_cast<uint32_t>(octet);
        start = end + 1;
    }
    bool saw_zero = false;
    for (int bit = 31; bit >= 0; --bit) {
        const bool set = (result & (static_cast<uint32_t>(1) << bit)) != 0;
        if (set && saw_zero) {
            return false;
        }
        if (!set) {
            saw_zero = true;
        }
    }
    return result != 0;
}

bool IsValidDns(const std::vector<std::string> &dns) {
    if (dns.size() > kMaxDnsServers) {
        return false;
    }
    for (const std::string &server : dns) {
        if (!IsValidIpv4(server)) {
            return false;
        }
    }
    return true;
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

bool ValidateConfig(const NetConfig &config,
                    bool allow_loopback_config) {
    if (!IsValidIfname(config.ifname)) {
        return false;
    }
    if (!allow_loopback_config && config.ifname == "lo") {
        return false;
    }
    if (!IsValidDns(config.dns)) {
        return false;
    }
    if (!config.dhcp) {
        if (!IsValidIpv4(config.static_ipv4.address) ||
            !IsValidNetmask(config.static_ipv4.netmask)) {
            return false;
        }
        if (!config.static_ipv4.gateway.empty() &&
            !IsValidIpv4(config.static_ipv4.gateway)) {
            return false;
        }
    }
    return true;
}

NetConfig DefaultConfig(const std::string &ifname) {
    NetConfig config;
    config.ifname = ifname.empty() ? "eth0" : ifname;
    config.enabled = true;
    config.dhcp = true;
    return config;
}

// JSON layout (per-interface object under "interfaces.<ifname>"):
// { "enabled": bool, "dhcp": bool,
//   "static_ipv4": { "address": str, "netmask": str, "gateway": str },
//   "dns": [str, ...] }

bool ConfigFromNetJson(const std::string &ifname,
                       const Json &value,
                       NetConfig *config) {
    if (!value.is_object() || config == nullptr) {
        return false;
    }
    NetConfig parsed;
    parsed.ifname = ifname;
    if (!json_reader::ReadField(value, "enabled", &parsed.enabled) ||
        !json_reader::ReadField(value, "dhcp", &parsed.dhcp)) {
        return false;
    }
    if (!value.contains("static_ipv4") ||
        !value.at("static_ipv4").is_object()) {
        return false;
    }
    const Json &static_ipv4 = value.at("static_ipv4");
    if (!json_reader::ReadField(static_ipv4, "address", &parsed.static_ipv4.address) ||
        !json_reader::ReadField(static_ipv4, "netmask", &parsed.static_ipv4.netmask) ||
        !json_reader::ReadField(static_ipv4, "gateway", &parsed.static_ipv4.gateway)) {
        return false;
    }
    if (!json_reader::ReadStringArray(value, "dns", &parsed.dns)) {
        return false;
    }
    *config = parsed;
    return true;
}

Json NetConfigToJson(const NetConfig &config) {
    Json value = Json::object();
    value["enabled"] = config.enabled;
    value["dhcp"] = config.dhcp;
    Json s = Json::object();
    s["address"] = config.static_ipv4.address;
    s["netmask"] = config.static_ipv4.netmask;
    s["gateway"] = config.static_ipv4.gateway;
    value["static_ipv4"] = s;
    Json dns = Json::array();
    for (const std::string &server : config.dns) {
        dns.push_back(server);
    }
    value["dns"] = dns;
    return value;
}

bool ConfigsFromNetworkJson(
    const Json &json,
    std::map<std::string, NetConfig> *configs) {
    if (configs == nullptr || !json.is_object()) {
        return false;
    }
    configs->clear();
    if (!json.contains("interfaces") || !json.at("interfaces").is_object()) {
        return false;
    }
    const Json &interfaces = json.at("interfaces");
    for (auto iter = interfaces.begin(); iter != interfaces.end(); ++iter) {
        NetConfig config;
        if (!ConfigFromNetJson(iter.key(), iter.value(), &config) ||
            !ValidateConfig(config, true)) {
            return false;
        }
        (*configs)[config.ifname] = config;
    }
    return true;
}

Json NetworkJsonWithConfigs(
    const Json &current,
    const std::map<std::string, NetConfig> &configs) {
    Json root = current.is_object() ? current : Json::object();
    Json interfaces = Json::object();
    for (const auto &entry : configs) {
        interfaces[entry.first] = NetConfigToJson(entry.second);
    }
    root["interfaces"] = interfaces;
    return root;
}

}  // namespace network_internal

// Public API JSON/format helpers.

Json NetInterfaceInfoToApiJson(const NetInterfaceInfo &interface_info) {
    Json root = Json::object();
    root["ifname"] = interface_info.ifname;
    root["enabled"] = interface_info.enabled;
    root["link_up"] = interface_info.link_up;
    root["dhcp"] = interface_info.dhcp;
    root["mac_address"] = interface_info.mac_address;
    root["last_ok"] = interface_info.last_ok;
    Json s = Json::object();
    s["address"] = interface_info.static_ipv4.address;
    s["prefix_length"] = interface_info.static_ipv4.prefix_length;
    s["netmask"] = interface_info.static_ipv4.netmask;
    s["gateway"] = interface_info.static_ipv4.gateway;
    root["static_ipv4"] = s;
    Json dns = Json::array();
    for (const std::string &server : interface_info.dns) {
        dns.push_back(server);
    }
    root["dns"] = dns;
    return root;
}

bool NetConfigFromApiJson(const std::string &ifname,
                          const Json &value,
                          NetConfig *config) {
    return network_internal::ConfigFromNetJson(ifname, value, config);
}

bool NetmaskToPrefixLength(const std::string &netmask, uint8_t *prefix_length) {
    if (prefix_length == nullptr || !network_internal::IsValidNetmask(netmask)) {
        return false;
    }
    uint32_t result = 0;
    std::size_t start = 0;
    while (start < netmask.size()) {
        std::size_t end = netmask.find('.', start);
        if (end == std::string::npos) {
            end = netmask.size();
        }
        int octet = 0;
        for (std::size_t i = start; i < end; ++i) {
            octet = octet * 10 + netmask[i] - '0';
        }
        result = (result << 8) | static_cast<uint32_t>(octet);
        start = end + 1;
    }
    uint8_t prefix = 0;
    for (int bit = 31; bit >= 0; --bit) {
        if ((result & (static_cast<uint32_t>(1) << bit)) != 0) {
            ++prefix;
        }
    }
    *prefix_length = prefix;
    return true;
}

std::string PrefixLengthToNetmask(uint8_t prefix_length) {
    if (prefix_length == 0 || prefix_length > 32) {
        return std::string();
    }
    const uint32_t mask =
        prefix_length == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix_length));
    return std::to_string((mask >> 24) & 0xff) + "." +
           std::to_string((mask >> 16) & 0xff) + "." +
           std::to_string((mask >> 8) & 0xff) + "." +
           std::to_string(mask & 0xff);
}

}  // namespace live_stream
