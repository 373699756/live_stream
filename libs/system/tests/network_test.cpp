#include "network_format.h"

#include "config.h"
#include "event.h"
#include "logger.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeNetworkPlatform : public live_stream::INetPlatform {
public:
    std::vector<std::string> ListInterfaces() override {
        ++list_count;
        return interfaces;
    }

    live_stream::NetInterfaceInfo GetInterfaceInfo(
        const std::string& ifname) override {
        ++status_count;
        status.ifname = ifname;
        return status;
    }

    bool SetInterfaceEnabled(const std::string& ifname,
                             bool enabled) override {
        ++enable_count;
        last_ifname = ifname;
        last_enabled = enabled;
        return enable_ok;
    }

    bool ApplyStaticAddress(
        const live_stream::NetConfig& config) override {
        ++static_count;
        last_static_config = config;
        return static_ok;
    }

    bool StartDhcp(const std::string& ifname) override {
        ++start_dhcp_count;
        last_ifname = ifname;
        return start_dhcp_ok;
    }

    bool StopDhcp(const std::string& ifname) override {
        ++stop_dhcp_count;
        last_ifname = ifname;
        return stop_dhcp_ok;
    }

    bool SetGateway(const std::string& ifname,
                    const std::string& gateway) override {
        ++gateway_count;
        last_ifname = ifname;
        last_gateway = gateway;
        return gateway_ok;
    }

    bool SetDnsServers(
        const std::vector<std::string>& dns_servers) override {
        ++dns_count;
        last_dns_servers = dns_servers;
        return dns_ok;
    }

    bool RollbackInterface(
        const live_stream::NetConfig& previous_config) override {
        ++rollback_count;
        last_rollback_config = previous_config;
        return rollback_ok;
    }

    std::vector<std::string> interfaces{"eth0"};
    live_stream::NetInterfaceInfo status;
    live_stream::NetConfig last_static_config;
    live_stream::NetConfig last_rollback_config;
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
    bool enable_ok = true;
    bool static_ok = true;
    bool start_dhcp_ok = true;
    bool stop_dhcp_ok = true;
    bool gateway_ok = true;
    bool dns_ok = true;
    bool rollback_ok = true;
};

class FakeConfig : public live_stream::IConfig {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::ConfigStatus Set(const std::string& name,
                                  const live_stream::ConfigJson& now,
                                  live_stream::ConfigIssue* issue) override {
        (void)issue;
        ++set_count;
        last_name = name;
        last_json = now.dump();
        if (scope.verify) {
            const live_stream::ConfigStatus status = scope.verify(now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        if (scope.apply) {
            const live_stream::ConfigJson prev = stored_json.empty()
                                                    ? live_stream::ConfigJson()
                                                    : live_stream::ConfigJson::parse(
                                                          stored_json, nullptr,
                                                          false);
            const live_stream::ConfigStatus status =
                scope.apply(prev, now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        if (!set_ok) {
            return live_stream::ConfigStatus::kSaveFailed;
        }
        stored_json = now.dump();
        return live_stream::ConfigStatus::kOk;
    }

    live_stream::ConfigJson Get(const std::string& name) override {
        ++get_count;
        last_name = name;
        if (stored_json.empty()) {
            return live_stream::ConfigJson();
        }
        return live_stream::ConfigJson::parse(stored_json, nullptr, false);
    }

    live_stream::ConfigStatus Reset(
        const std::string& name, live_stream::ConfigIssue*) override {
        return name == "network" ? live_stream::ConfigStatus::kOk
                                 : live_stream::ConfigStatus::kNotFound;
    }

    live_stream::ConfigJson Default(const std::string&) override {
        return live_stream::ConfigJson();
    }

    live_stream::ConfigStatus ResetAll(
        live_stream::ConfigIssue*) override {
        return live_stream::ConfigStatus::kOk;
    }

    bool AddScope(const std::string& name,
                  const live_stream::ConfigScope& next) override {
        if (name != "network" || attached) {
            return false;
        }
        scope = next;
        attached = true;
        return true;
    }

    bool RemoveScope(const std::string& name) override {
        if (name != "network" || !attached) {
            return false;
        }
        scope = live_stream::ConfigScope();
        attached = false;
        return true;
    }

    std::string stored_json;
    std::string last_name;
    std::string last_json;
    live_stream::ConfigScope scope;
    int set_count = 0;
    int get_count = 0;
    bool set_ok = true;
    bool attached = false;
};

class FakeEvent : public live_stream::event::Dispatcher {
public:
    FakeEvent()
        : subscription_(Subscribe(
              live_stream::event::EventType::kNetworkChanged,
              [this](const live_stream::event::Event& event) {
                  ++publish_count;
                  last_event = event;
              })) {}

    int publish_count = 0;
    live_stream::event::Event last_event;

private:
    live_stream::event::Subscription subscription_;
};

class FakeLogger : public live_stream::ILogger {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

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

live_stream::NetConfig DhcpConfig() {
    live_stream::NetConfig config;
    config.ifname = "eth0";
    config.enabled = true;
    config.dhcp = true;
    config.dns.push_back("8.8.8.8");
    return config;
}

live_stream::NetConfig StaticConfig() {
    live_stream::NetConfig config;
    config.ifname = "eth0";
    config.enabled = true;
    config.dhcp = false;
    config.static_ipv4.address = "192.168.1.20";
    config.static_ipv4.netmask = "255.255.255.0";
    config.static_ipv4.gateway = "192.168.1.1";
    config.dns.push_back("1.1.1.1");
    return config;
}

std::unique_ptr<live_stream::INetwork> CreateStarted(
    FakeNetworkPlatform* platform,
    FakeConfig* config,
    FakeEvent* event,
    FakeLogger* logger) {
    live_stream::NetOptions options;
    options.platform = platform;
    options.config = config;
    options.event = event;
    options.logger = logger;
    options.default_ifname = "eth0";
    std::unique_ptr<live_stream::INetwork> service =
        live_stream::CreateNetwork(options);
    if (!service || !service->Start()) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::NetworkName(), "system.network") !=
        0) {
        return 1;
    }

    live_stream::NetOptions empty_options;
    std::unique_ptr<live_stream::INetwork> default_network =
        live_stream::CreateNetwork(empty_options);
    if (!default_network || !default_network->Start()) {
        return 2;
    }
    default_network->Stop();
    default_network->Stop();

    FakeNetworkPlatform platform;
    platform.status.ifname = "eth0";
    platform.status.link_up = true;
    platform.status.enabled = true;
    platform.status.dhcp = true;
    platform.status.static_ipv4.address = "192.168.1.10";
    platform.status.static_ipv4.prefix_length = 24;
    platform.status.static_ipv4.netmask = "255.255.255.0";
    platform.status.static_ipv4.gateway = "192.168.1.1";
    platform.status.mac_address = "00:11:22:33:44:55";
    platform.status.dns.push_back("8.8.8.8");

    FakeConfig config;
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::INetwork> service =
        CreateStarted(&platform, &config, &event,
                      &logger);
    if (!service || !config.attached) {
        return 3;
    }

    std::vector<std::string> interfaces = service->GetInterfaces();
    if (interfaces.size() != 1 || interfaces[0] != "eth0") {
        return 4;
    }

    live_stream::NetInterfaceInfo status =
        service->GetInterfaceInfo("eth0");
    if (!status.link_up ||
        status.static_ipv4.address != "192.168.1.10" ||
        status.mac_address != "00:11:22:33:44:55") {
        return 5;
    }

    live_stream::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    context.session_id = "session-1";
    context.client_ip = "10.0.0.2";

    if (!service->ApplyInterfaceConfig(context, DhcpConfig()) ||
        platform.enable_count != 2 || !platform.last_enabled ||
        platform.start_dhcp_count != 2 || platform.gateway_count != 2 ||
        !platform.last_gateway.empty() || platform.dns_count != 2 ||
        config.set_count != 1 ||
        config.last_name != "network" ||
        event.last_event.type !=
            live_stream::event::EventType::kNetworkChanged ||
        logger.last_record.result !=
            live_stream::OperationResult::kSuccess ||
        logger.last_record.action !=
            live_stream::OperationAction::kNetworkChange) {
        return 6;
    }

    const int publish_count_after_dhcp = event.publish_count;
    if (!service->ApplyInterfaceConfig(context, StaticConfig()) ||
        platform.stop_dhcp_count != 1 || platform.static_count != 1 ||
        platform.gateway_count != 3 ||
        platform.last_gateway != "192.168.1.1" ||
        config.last_json.find("\"dhcp\":false") ==
            std::string::npos ||
        event.publish_count != publish_count_after_dhcp + 1) {
        return 7;
    }

    live_stream::NetConfig invalid = StaticConfig();
    invalid.static_ipv4.address = "999.1.1.1";
    const int static_count_before_invalid = platform.static_count;
    if (service->ApplyInterfaceConfig(context, invalid) ||
        platform.static_count != static_count_before_invalid ||
        logger.last_record.result !=
            live_stream::OperationResult::kRejected) {
        return 8;
    }

    live_stream::NetConfig loopback = StaticConfig();
    loopback.ifname = "lo";
    if (service->ApplyInterfaceConfig(context, loopback) ||
        logger.last_record.result !=
            live_stream::OperationResult::kRejected) {
        return 9;
    }

    platform.static_ok = false;
    const int publish_count_before_failure = event.publish_count;
    if (service->ApplyInterfaceConfig(context, StaticConfig()) ||
        platform.rollback_count != 1 ||
        event.publish_count != publish_count_before_failure ||
        logger.last_record.result !=
            live_stream::OperationResult::kFailed) {
        return 10;
    }

    platform.static_ok = true;
    config.set_ok = false;
    const int rollback_count_before_persist_failure = platform.rollback_count;
    const int publish_count_before_persist_failure = event.publish_count;
    if (service->ApplyInterfaceConfig(context, StaticConfig()) ||
        platform.rollback_count != rollback_count_before_persist_failure + 1 ||
        event.publish_count != publish_count_before_persist_failure ||
        logger.last_record.result !=
            live_stream::OperationResult::kFailed) {
        return 11;
    }
    config.set_ok = true;

    status = service->GetInterfaceInfo("eth0");
    if (status.last_ok) {
        return 12;
    }

    platform.status.last_ok = false;
    if (!service->ReloadInterfaceInfo()) {
        return 13;
    }
    status = service->GetInterfaceInfo("eth0");
    if (status.last_ok) {
        return 14;
    }

    live_stream::ConfigJson status_json =
        live_stream::NetInterfaceInfoToApiJson(status);
    if (status_json["static_ipv4"]["address"] != "192.168.1.10") {
        return 15;
    }

    live_stream::NetConfig parsed;
    if (!live_stream::NetConfigFromApiJson(
            "eth0", status_json, &parsed) ||
        parsed.static_ipv4.address != "192.168.1.10") {
        return 16;
    }

    service->Stop();
    return 0;
}
