#ifndef LIVE_STREAM_SYSTEM_NETWORK_H_
#define LIVE_STREAM_SYSTEM_NETWORK_H_

#include "event.h"
#include "request_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfig;
class ILogger;

struct NetIpv4Config {
    std::string address;
    std::string netmask;  // e.g. "255.255.255.0"
    std::string gateway;
};

struct NetConfig {
    std::string ifname;
    bool enabled = true;
    bool dhcp = true;
    NetIpv4Config static_ipv4;
    std::vector<std::string> dns;
};

struct NetInterfaceInfo {
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

class INetPlatform {
public:
    virtual ~INetPlatform() = default;

    virtual std::vector<std::string> ListInterfaces() = 0;
    virtual NetInterfaceInfo GetInterfaceInfo(
        const std::string& ifname) = 0;
    virtual bool SetInterfaceEnabled(const std::string& ifname,
                                     bool enabled) = 0;
    virtual bool ApplyStaticAddress(
        const NetConfig& config) = 0;
    virtual bool StartDhcp(const std::string& ifname) = 0;
    virtual bool StopDhcp(const std::string& ifname) = 0;
    // Passing an empty gateway clears the interface default route.
    virtual bool SetGateway(const std::string& ifname,
                            const std::string& gateway) = 0;
    virtual bool SetDnsServers(
        const std::vector<std::string>& dns_servers) = 0;
    virtual bool RollbackInterface(
        const NetConfig& previous_config) = 0;
};

struct NetOptions {
    IConfig* config = nullptr;
    event::Dispatcher* event = nullptr;
    ILogger* logger = nullptr;
    INetPlatform* platform = nullptr;
    std::string default_ifname = "eth0";
    bool allow_loopback_config = false;
};

class INetwork {
public:
    virtual ~INetwork() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual std::vector<std::string> GetInterfaces() = 0;
    virtual NetInterfaceInfo GetInterfaceInfo(
        const std::string& ifname) = 0;
    virtual bool ApplyInterfaceConfig(
        const live_stream::RequestContext& context,
        const NetConfig& config) = 0;
    virtual bool ReloadInterfaceInfo() = 0;
};

std::unique_ptr<INetwork> CreateNetwork(
    const NetOptions& options);

const char* NetworkName();

}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_NETWORK_H_
