#ifndef LIVE_STREAM_NETWORK_SERVICE_H_
#define LIVE_STREAM_NETWORK_SERVICE_H_

#include "infra/status.h"
#include "infra/request_context.h"
#include "infra/service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfigService;
class IEventService;
class ILoggerService;

enum class NetworkAddressMode {
    kDhcp,
    kStatic,
};

struct NetworkInterfaceConfig {
    std::string ifname;
    bool enabled = true;
    NetworkAddressMode address_mode = NetworkAddressMode::kDhcp;
    std::string ipv4_address;
    uint8_t prefix_length = 24;
    std::string gateway;
    std::vector<std::string> dns_servers;
};

struct NetworkInterfaceStatus {
    std::string ifname;
    bool enabled = true;
    bool link_up = false;
    bool dhcp_enabled = true;
    std::string ipv4_address;
    uint8_t prefix_length = 0;
    std::string mac_address;
    std::string gateway;
    std::vector<std::string> dns_servers;
    infra::Status last_error = infra::Status::kOk;
};

class INetworkPlatform {
 public:
    virtual ~INetworkPlatform() = default;

    virtual infra::Result<std::vector<std::string>> ListInterfaces() = 0;
    virtual infra::Result<NetworkInterfaceStatus> GetInterfaceStatus(
        const std::string& ifname) = 0;
    virtual infra::Status SetInterfaceEnabled(const std::string& ifname,
                                             bool enabled) = 0;
    virtual infra::Status ApplyStaticAddress(
        const NetworkInterfaceConfig& config) = 0;
    virtual infra::Status StartDhcp(const std::string& ifname) = 0;
    virtual infra::Status StopDhcp(const std::string& ifname) = 0;
    // Passing an empty gateway clears the interface default route.
    virtual infra::Status SetGateway(const std::string& ifname,
                                    const std::string& gateway) = 0;
    virtual infra::Status SetDnsServers(
        const std::vector<std::string>& dns_servers) = 0;
    virtual infra::Status RollbackInterface(
        const NetworkInterfaceConfig& previous_config) = 0;
};

struct NetworkServiceOptions {
    IConfigService* config_service = nullptr;
    IEventService* event_service = nullptr;
    ILoggerService* logger_service = nullptr;
    INetworkPlatform* platform = nullptr;
    std::string default_ifname = "eth0";
    bool allow_loopback_config = false;
};

class INetworkService : public infra::IService {
 public:
    virtual infra::Result<std::vector<std::string>> GetInterfaces() = 0;
    virtual infra::Result<NetworkInterfaceStatus> GetInterfaceStatus(
        const std::string& ifname) = 0;
    virtual infra::Status ApplyInterfaceConfig(
        const infra::RequestContext& context,
        const NetworkInterfaceConfig& config) = 0;
    virtual infra::Status ReloadStatus() = 0;
};

std::unique_ptr<INetworkService> CreateNetworkService(
    const NetworkServiceOptions& options);

const char* NetworkAddressModeToString(NetworkAddressMode mode);

class NetworkService {
 public:
    static const char* Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_NETWORK_SERVICE_H_
