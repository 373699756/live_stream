#ifndef LIVE_STREAM_NETWORK_CONFIG_NETWORK_CONFIG_H_
#define LIVE_STREAM_NETWORK_CONFIG_NETWORK_CONFIG_H_

#include "config_json.h"
#include "request_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfig;
class IEvent;
class ILogger;

struct StaticIpv4Config {
    std::string address;
    std::string netmask;  // e.g. "255.255.255.0"
    std::string gateway;
};

struct NetworkInterfaceConfig {
    std::string ifname;
    bool enabled = true;
    bool dhcp = true;
    StaticIpv4Config static_ipv4;
    std::vector<std::string> dns;
};

struct NetworkInterfaceStatus {
    std::string ifname;
    bool enabled = true;
    bool link_up = false;
    bool dhcp = true;
    std::string mac_address;
    bool last_ok = true;
    struct {
        std::string address;
        uint8_t prefix_length = 0;
        std::string netmask;
        std::string gateway;
    } static_ipv4;
    std::vector<std::string> dns;
};

class INetworkPlatform {
public:
    virtual ~INetworkPlatform() = default;

    virtual std::vector<std::string> ListInterfaces() = 0;
    virtual NetworkInterfaceStatus GetInterfaceStatus(
        const std::string& ifname) = 0;
    virtual bool SetInterfaceEnabled(const std::string& ifname,
                                     bool enabled) = 0;
    virtual bool ApplyStaticAddress(
        const NetworkInterfaceConfig& config) = 0;
    virtual bool StartDhcp(const std::string& ifname) = 0;
    virtual bool StopDhcp(const std::string& ifname) = 0;
    // Passing an empty gateway clears the interface default route.
    virtual bool SetGateway(const std::string& ifname,
                            const std::string& gateway) = 0;
    virtual bool SetDnsServers(
        const std::vector<std::string>& dns_servers) = 0;
    virtual bool RollbackInterface(
        const NetworkInterfaceConfig& previous_config) = 0;
};

struct NetworkConfigOptions {
    IConfig* config = nullptr;
    IEvent* event = nullptr;
    ILogger* logger = nullptr;
    INetworkPlatform* platform = nullptr;
    std::string default_ifname = "eth0";
    bool allow_loopback_config = false;
};

class INetworkConfig {
public:
    virtual ~INetworkConfig() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual std::vector<std::string> GetInterfaces() = 0;
    virtual NetworkInterfaceStatus GetInterfaceStatus(
        const std::string& ifname) = 0;
    virtual bool ApplyInterfaceConfig(
        const live_stream::RequestContext& context,
        const NetworkInterfaceConfig& config) = 0;
    virtual bool ReloadStatus() = 0;
};

std::unique_ptr<INetworkConfig> CreateNetworkConfig(
    const NetworkConfigOptions& options);

ConfigJson NetworkInterfaceStatusToApiJson(
    const NetworkInterfaceStatus& status);
bool NetworkInterfaceConfigFromApiJson(const std::string& ifname,
                                       const ConfigJson& value,
                                       NetworkInterfaceConfig* config);

class NetworkConfig {
public:
    static const char* Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_NETWORK_CONFIG_NETWORK_CONFIG_H_
