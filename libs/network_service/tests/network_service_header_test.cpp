#include "network_service.h"

#include "config_service.h"
#include "event_service.h"
#include "logger_service.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeNetworkPlatform : public live_stream::INetworkPlatform {
 public:
    std::vector<std::string> ListInterfaces() override {
        ++list_count;
        return interfaces;
    }

    live_stream::NetworkInterfaceStatus GetInterfaceStatus(
        const std::string& ifname) override {
        ++status_count;
        status.ifname = ifname;
        return status;
    }

    bool ApplyStaticAddress(
        const live_stream::NetworkInterfaceConfig& config) override {
        ++static_count;
        last_static_config = config;
        return static_error;
    }

    bool SetInterfaceEnabled(const std::string& ifname,
                            bool enabled) override {
        ++enable_count;
        last_ifname = ifname;
        last_enabled = enabled;
        return enable_error;
    }

    bool StartDhcp(const std::string& ifname) override {
        ++start_dhcp_count;
        last_ifname = ifname;
        return start_dhcp_error;
    }

    bool StopDhcp(const std::string& ifname) override {
        ++stop_dhcp_count;
        last_ifname = ifname;
        return stop_dhcp_error;
    }

    bool SetGateway(const std::string& ifname,
                    const std::string& gateway) override {
        ++gateway_count;
        last_ifname = ifname;
        last_gateway = gateway;
        return gateway_error;
    }

    bool SetDnsServers(
        const std::vector<std::string>& dns_servers) override {
        ++dns_count;
        last_dns_servers = dns_servers;
        return dns_error;
    }

    bool RollbackInterface(
        const live_stream::NetworkInterfaceConfig& previous_config) override {
        ++rollback_count;
        last_rollback_config = previous_config;
        return rollback_error;
    }

    std::vector<std::string> interfaces{"eth0"};
    live_stream::NetworkInterfaceStatus status;
    live_stream::NetworkInterfaceConfig last_static_config;
    live_stream::NetworkInterfaceConfig last_rollback_config;
    std::vector<std::string> last_dns_servers;
    std::string last_ifname;
    std::string last_gateway;
    int list_count = 0;
    int status_count = 0;
    int static_count = 0;
    int enable_count = 0;
    int start_dhcp_count = 0;
    int stop_dhcp_count = 0;
    int gateway_count = 0;
    int dns_count = 0;
    int rollback_count = 0;
    bool last_enabled = true;
    bool enable_error = true;
    bool static_error = true;
    bool start_dhcp_error = true;
    bool stop_dhcp_error = true;
    bool gateway_error = true;
    bool dns_error = true;
    bool rollback_error = true;
};

class FakeConfigService : public live_stream::IConfigService {
 public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    bool SetValue(const std::string& name,
                  const live_stream::ConfigJson& value) override {
        ++set_count;
        last_name = name;
        last_json = value.dump();
        for (const live_stream::ConfigProc& proc : verify_callbacks) {
            if (!proc(value)) {
                return false;
            }
        }
        for (const live_stream::ConfigProc& proc : apply_callbacks) {
            if (!proc(value)) {
                return false;
            }
        }
        return set_error;
    }

    live_stream::ConfigJson GetValue(const std::string& name) override {
        ++get_count;
        last_name = name;
        if (!get_ok) {
            return live_stream::ConfigJson();
        }
        return live_stream::ConfigJson::parse(stored_json, nullptr, false);
    }

    live_stream::ConfigJson GetDefault(const std::string&) override {
        return live_stream::ConfigJson();
    }
    bool RestoreDefaults() override { return true; }
    bool SaveFile() override { return true; }
    bool RegisterApply(const std::string&,
                       live_stream::ConfigProc proc) override {
        apply_callbacks.push_back(proc);
        return true;
    }
    bool RegisterVerify(const std::string&,
                        live_stream::ConfigProc proc) override {
        verify_callbacks.push_back(proc);
        return true;
    }

    std::string stored_json;
    std::string last_name;
    std::string last_json;
    std::vector<live_stream::ConfigProc> apply_callbacks;
    std::vector<live_stream::ConfigProc> verify_callbacks;
    int set_count = 0;
    int get_count = 0;
    bool get_ok = false;
    bool set_error = true;
};

class FakeEventService : public live_stream::IEventService {
 public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    live_stream::EventSubscriptionId Subscribe(
        live_stream::EventType, live_stream::EventHandler) override {
        return 1;
    }

    bool Unsubscribe(live_stream::EventSubscriptionId) override {
        return true;
    }

    bool Publish(const live_stream::Event& event) override {
        ++publish_count;
        last_event = event;
        return true;
    }

    int publish_count = 0;
    live_stream::Event last_event;
};

class FakeLoggerService : public live_stream::ILoggerService {
 public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    bool RecordOperation(
        const live_stream::OperationRecord& record) override {
        ++record_count;
        last_record = record;
        return true;
    }

    std::vector<live_stream::OperationRecord> QueryOperations(
        const live_stream::OperationLogQuery&) override {
        return {};
    }

    bool ExportOperations(
        const live_stream::OperationLogExportOptions&) override {
        return true;
    }

    int record_count = 0;
    live_stream::OperationRecord last_record;
};

std::unique_ptr<live_stream::INetworkService> CreateStartedService(
    FakeNetworkPlatform* platform,
    FakeConfigService* config_service,
    FakeEventService* event_service,
    FakeLoggerService* logger_service) {
    live_stream::NetworkServiceOptions options;
    options.platform = platform;
    options.config_service = config_service;
    options.event_service = event_service;
    options.logger_service = logger_service;
    options.default_ifname = "eth0";
    std::unique_ptr<live_stream::INetworkService> service =
        live_stream::CreateNetworkService(options);
    if (!service || !service->Init() || !service->Start()) {
        return nullptr;
    }
    return service;
}

live_stream::NetworkInterfaceConfig DhcpConfig() {
    live_stream::NetworkInterfaceConfig config;
    config.ifname = "eth0";
    config.address_mode = live_stream::NetworkAddressMode::kDhcp;
    config.dns_servers.push_back("8.8.8.8");
    return config;
}

live_stream::NetworkInterfaceConfig StaticConfig() {
    live_stream::NetworkInterfaceConfig config;
    config.ifname = "eth0";
    config.address_mode = live_stream::NetworkAddressMode::kStatic;
    config.ipv4_address = "192.168.1.20";
    config.prefix_length = 24;
    config.gateway = "192.168.1.1";
    config.dns_servers.push_back("1.1.1.1");
    return config;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::NetworkService::Name(), "network_service") !=
        0) {
        return 1;
    }

    live_stream::NetworkServiceOptions empty_options;
    std::unique_ptr<live_stream::INetworkService> default_service =
        live_stream::CreateNetworkService(empty_options);
    if (!default_service || !default_service->Init() ||
        !default_service->Start()) {
        return 2;
    }
    default_service->Stop();
    default_service->Stop();
    default_service->Deinit();
    default_service->Deinit();

    FakeNetworkPlatform platform;
    platform.status.link_up = true;
    platform.status.enabled = true;
    platform.status.dhcp_enabled = true;
    platform.status.ipv4_address = "192.168.1.10";
    platform.status.prefix_length = 24;
    platform.status.mac_address = "00:11:22:33:44:55";
    platform.status.gateway = "192.168.1.1";
    platform.status.dns_servers.push_back("8.8.8.8");
    FakeConfigService config_service;
    FakeEventService event_service;
    FakeLoggerService logger_service;
    std::unique_ptr<live_stream::INetworkService> service =
        CreateStartedService(&platform, &config_service, &event_service,
                             &logger_service);
    if (!service) {
        return 3;
    }

    std::vector<std::string> interfaces = service->GetInterfaces();
    if (interfaces.size() != 1 || interfaces[0] != "eth0") {
        return 4;
    }

    live_stream::NetworkInterfaceStatus status =
        service->GetInterfaceStatus("eth0");
    if (!status.link_up || status.ipv4_address != "192.168.1.10" ||
        status.mac_address != "00:11:22:33:44:55") {
        return 5;
    }

    live_stream::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    context.session_id = "session-1";
    context.client_ip = "10.0.0.2";

    if (!service->ApplyInterfaceConfig(context, DhcpConfig()) ||
        platform.enable_count != 1 || !platform.last_enabled ||
        platform.start_dhcp_count != 1 || platform.gateway_count != 1 ||
        !platform.last_gateway.empty() || platform.dns_count != 1 ||
        config_service.set_count != 1 || config_service.last_name != "network" ||
        event_service.last_event.type != live_stream::EventType::kNetworkChanged ||
        logger_service.last_record.result !=
            live_stream::OperationResult::kSuccess ||
        logger_service.last_record.action !=
            live_stream::OperationAction::kNetworkChange) {
        return 6;
    }

    const int publish_count_after_dhcp = event_service.publish_count;
    if (!service->ApplyInterfaceConfig(context, StaticConfig()) ||
        platform.stop_dhcp_count != 1 || platform.static_count != 1 ||
        platform.gateway_count != 2 || platform.last_gateway != "192.168.1.1" ||
        config_service.last_json.find("\"dhcp\":false") ==
            std::string::npos ||
        event_service.publish_count != publish_count_after_dhcp + 1) {
        return 7;
    }

    live_stream::NetworkInterfaceConfig disabled = DhcpConfig();
    disabled.enabled = false;
    const int start_dhcp_before_disabled = platform.start_dhcp_count;
    if (!service->ApplyInterfaceConfig(context, disabled) ||
        platform.enable_count != 3 || platform.last_enabled ||
        platform.start_dhcp_count != start_dhcp_before_disabled ||
        config_service.last_json.find("\"enabled\":false") == std::string::npos) {
        return 14;
    }

    live_stream::NetworkInterfaceConfig invalid_mode = DhcpConfig();
    invalid_mode.address_mode =
        static_cast<live_stream::NetworkAddressMode>(99);
    const int enable_count_before_invalid_mode = platform.enable_count;
    if (service->ApplyInterfaceConfig(context, invalid_mode) ||
        platform.enable_count != enable_count_before_invalid_mode ||
        logger_service.last_record.result !=
            live_stream::OperationResult::kRejected) {
        return 15;
    }

    live_stream::NetworkInterfaceConfig invalid = StaticConfig();
    invalid.ipv4_address = "999.1.1.1";
    const int static_count_before_invalid = platform.static_count;
    if (service->ApplyInterfaceConfig(context, invalid) ||
        platform.static_count != static_count_before_invalid ||
        logger_service.last_record.result !=
            live_stream::OperationResult::kRejected) {
        return 8;
    }

    live_stream::NetworkInterfaceConfig loopback = StaticConfig();
    loopback.ifname = "lo";
    if (service->ApplyInterfaceConfig(context, loopback) ||
        logger_service.last_record.result !=
            live_stream::OperationResult::kRejected) {
        return 9;
    }

    platform.static_error = false;
    const int publish_count_before_failure = event_service.publish_count;
    if (service->ApplyInterfaceConfig(context, StaticConfig()) ||
        platform.rollback_count != 1 ||
        event_service.publish_count != publish_count_before_failure ||
        logger_service.last_record.result !=
            live_stream::OperationResult::kFailed) {
        return 10;
    }

    platform.static_error = true;
    config_service.set_error = false;
    const int rollback_count_before_persist_failure = platform.rollback_count;
    const int publish_count_before_persist_failure = event_service.publish_count;
    if (service->ApplyInterfaceConfig(context, StaticConfig()) ||
        platform.rollback_count != rollback_count_before_persist_failure + 1 ||
        event_service.publish_count != publish_count_before_persist_failure ||
        logger_service.last_record.result !=
            live_stream::OperationResult::kFailed) {
        return 16;
    }
    config_service.set_error = true;

    status = service->GetInterfaceStatus("eth0");
    if (status.last_ok) {
        return 11;
    }

    platform.static_error = true;
    platform.status.last_ok = false;
    if (!service->ReloadStatus()) {
        return 12;
    }
    status = service->GetInterfaceStatus("eth0");
    if (status.last_ok) {
        return 13;
    }

    service->Stop();
    service->Deinit();
    return 0;
}
