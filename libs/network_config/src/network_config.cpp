#include "network_config.h"

#include "config.h"
#include "event.h"
#include "infra/time.h"
#include "logger.h"
#include "network_config_codec.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char *kConfigName = "network";

using network_internal::ConfigFromNetworkInterfaceJson;
using network_internal::ConfigsFromNetworkJson;
using network_internal::DefaultConfig;
using network_internal::IsValidIfname;
using network_internal::NetworkJsonWithConfigs;
using network_internal::ValidateConfig;

OperationResult ToOperationResult(bool ok, bool rejected) {
    if (rejected) {
        return OperationResult::kRejected;
    }
    return ok ? OperationResult::kSuccess : OperationResult::kFailed;
}

class RestrictedNetworkPlatform : public INetworkPlatform {
public:
    explicit RestrictedNetworkPlatform(std::string default_ifname)
        : default_ifname_(std::move(default_ifname)) {}

    std::vector<std::string> ListInterfaces() override {
        return {default_ifname_};
    }

    NetworkInterfaceStatus
    GetInterfaceStatus(const std::string &ifname) override {
        NetworkInterfaceStatus status;
        status.ifname = ifname;
        status.enabled = true;
        status.dhcp = true;
        status.last_ok = false;
        return status;
    }

    bool ApplyStaticAddress(const NetworkInterfaceConfig &) override {
        return false;
    }

    bool SetInterfaceEnabled(const std::string &, bool) override { return false; }

    bool StartDhcp(const std::string &) override { return false; }

    bool StopDhcp(const std::string &) override { return false; }

    bool SetGateway(const std::string &, const std::string &) override {
        return false;
    }

    bool SetDnsServers(const std::vector<std::string> &) override {
        return false;
    }

    bool RollbackInterface(const NetworkInterfaceConfig &) override {
        return true;
    }

private:
    std::string default_ifname_;
};

class NetworkConfigImpl : public INetworkConfig {
public:
    explicit NetworkConfigImpl(const NetworkConfigOptions &options)
        : options_(options),
          owned_platform_(
              options.platform == nullptr
                  ? new RestrictedNetworkPlatform(options.default_ifname.empty()
                                                      ? "eth0"
                                                      : options.default_ifname)
                  : nullptr),
          platform_(options.platform != nullptr ? options.platform
                                                : owned_platform_.get()) {}

    bool Prepare() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) {
                return true;
            }
            if (platform_ == nullptr || options_.default_ifname.empty()) {
                return false;
            }
            configs_.clear();
        }
        if (!LoadConfigs()) {
            return false;
        }
        if (!ApplyCurrentConfigs()) {
            return false;
        }
        if (options_.config != nullptr && !config_attached_) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                return VerifyNetworkConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid network config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                return ApplyNetworkConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "apply network config failed");
            };
            if (!options_.config->AttachConfig(kConfigName, attachment)) {
                return false;
            }
            config_attached_ = true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = true;
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    bool IsStarted() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void Release() {
        bool detach = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            detach = config_attached_;
            status_errors_.clear();
            configs_.clear();
            suppress_config_apply_ = false;
            config_attached_ = false;
            started_ = false;
            initialized_ = false;
        }
        if (detach && options_.config != nullptr) {
            static_cast<void>(options_.config->DetachConfig(kConfigName));
        }
    }

    std::vector<std::string> GetInterfaces() override {
        if (!IsStarted()) {
            return {};
        }
        return platform_->ListInterfaces();
    }

    NetworkInterfaceStatus
    GetInterfaceStatus(const std::string &ifname) override {
        if (!IsValidIfname(ifname)) {
            return NetworkInterfaceStatus{};
        }
        if (!IsStarted()) {
            return NetworkInterfaceStatus{};
        }
        NetworkInterfaceStatus status = platform_->GetInterfaceStatus(ifname);
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iter = status_errors_.find(ifname);
        if (iter != status_errors_.end()) {
            status.last_ok = iter->second;
        }
        return status;
    }

    bool ApplyInterfaceConfig(const live_stream::RequestContext &context,
                              const NetworkInterfaceConfig &config) override {
        if (!ValidateConfig(config, options_.allow_loopback_config)) {
            RecordAudit(context, config.ifname, false, true, "invalid_config");
            return false;
        }
        if (!IsStarted()) {
            RecordAudit(context, config.ifname, false, false, "not_started");
            return false;
        }

        NetworkInterfaceConfig previous_config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto iter = configs_.find(config.ifname);
            previous_config =
                iter == configs_.end() ? DefaultConfig(config.ifname) : iter->second;
        }
        if (!ApplyToPlatform(config)) {
            const bool rollback_ok = platform_->RollbackInterface(previous_config);
            SetLastError(config.ifname, false);
            RecordAudit(context, config.ifname, false, false,
                        AuditReason("apply_failed", rollback_ok));
            return false;
        }

        if (!PersistConfig(config)) {
            const bool rollback_ok = platform_->RollbackInterface(previous_config);
            SetLastError(config.ifname, false);
            RecordAudit(context, config.ifname, false, false,
                        AuditReason("persist_failed", rollback_ok));
            return false;
        }

        SetLastError(config.ifname, true);
        PublishNetworkChanged(config.ifname);
        RecordAudit(context, config.ifname, true, false, "ok");
        return true;
    }

    bool ReloadStatus() override {
        if (!IsStarted()) {
            return false;
        }
        std::vector<std::string> interfaces = platform_->ListInterfaces();
        for (const std::string &ifname : interfaces) {
            NetworkInterfaceStatus status = platform_->GetInterfaceStatus(ifname);
            SetLastError(ifname, status.last_ok);
        }
        return true;
    }

private:
    bool LoadConfigs() {
        ConfigJson network_json = ConfigJson::object();
        std::map<std::string, NetworkInterfaceConfig> loaded_configs;
        if (options_.config == nullptr) {
            NetworkInterfaceConfig config = DefaultConfig(options_.default_ifname);
            loaded_configs[config.ifname] = config;
            network_json = NetworkJsonWithConfigs(network_json, loaded_configs);
        } else {
            ConfigJson json = options_.config->GetValue(kConfigName);
            if (!json.is_object()) {
                NetworkInterfaceConfig config = DefaultConfig(options_.default_ifname);
                loaded_configs[config.ifname] = config;
                network_json = NetworkJsonWithConfigs(network_json, loaded_configs);
            } else if (!ConfigsFromNetworkJson(json, &loaded_configs)) {
                return false;
            } else {
                network_json = json;
            }
        }
        if (loaded_configs.empty()) {
            NetworkInterfaceConfig config = DefaultConfig(options_.default_ifname);
            loaded_configs[config.ifname] = config;
            network_json = NetworkJsonWithConfigs(network_json, loaded_configs);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        configs_ = loaded_configs;
        network_config_json_ = network_json;
        return true;
    }

    bool ApplyToPlatform(const NetworkInterfaceConfig &config) {
        if (!platform_->SetInterfaceEnabled(config.ifname, config.enabled)) {
            return false;
        }
        if (!config.enabled) {
            return true;
        }
        if (config.dhcp) {
            if (!platform_->StartDhcp(config.ifname)) {
                return false;
            }
        } else {
            if (!platform_->StopDhcp(config.ifname) ||
                !platform_->ApplyStaticAddress(config)) {
                return false;
            }
        }
        return platform_->SetGateway(config.ifname,
                                     config.static_ipv4.gateway) &&
               platform_->SetDnsServers(config.dns);
    }

    bool ApplyCurrentConfigs() {
        std::map<std::string, NetworkInterfaceConfig> configs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            configs = configs_;
        }
        for (const auto &entry : configs) {
            if (!ApplyToPlatform(entry.second)) {
                SetLastError(entry.first, false);
                return false;
            }
            SetLastError(entry.first, true);
        }
        return true;
    }

    bool PersistConfig(const NetworkInterfaceConfig &config) {
        std::map<std::string, NetworkInterfaceConfig> next_configs;
        ConfigJson next_json;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_configs = configs_;
            next_configs[config.ifname] = config;
            next_json = network_config_json_.is_object() ? network_config_json_
                                                         : ConfigJson::object();
        }
        next_json = NetworkJsonWithConfigs(next_json, next_configs);
        if (options_.config != nullptr) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                suppress_config_apply_ = true;
            }
            const bool ok = options_.config->SetValue(kConfigName, next_json);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                suppress_config_apply_ = false;
            }
            if (!ok) {
                return false;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        configs_ = next_configs;
        network_config_json_ = next_json;
        return true;
    }

    bool VerifyNetworkConfig(const ConfigJson &json) {
        std::map<std::string, NetworkInterfaceConfig> parsed;
        if (!ConfigsFromNetworkJson(json, &parsed)) {
            return false;
        }
        for (const auto &entry : parsed) {
            if (!ValidateConfig(entry.second, options_.allow_loopback_config)) {
                return false;
            }
        }
        return true;
    }

    bool ApplyNetworkConfig(const ConfigJson &json) {
        std::map<std::string, NetworkInterfaceConfig> parsed;
        if (!ConfigsFromNetworkJson(json, &parsed)) {
            return false;
        }
        if (parsed.empty()) {
            NetworkInterfaceConfig config = DefaultConfig(options_.default_ifname);
            parsed[config.ifname] = config;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (suppress_config_apply_) {
                return true;
            }
        }
        if (!ApplyConfigChanges(parsed)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        configs_ = parsed;
        network_config_json_ = NetworkJsonWithConfigs(json, parsed);
        return true;
    }

    bool ApplyConfigChanges(
        const std::map<std::string, NetworkInterfaceConfig> &next_configs) {
        std::map<std::string, NetworkInterfaceConfig> previous_configs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previous_configs = configs_;
        }
        std::vector<std::string> applied_ifnames;
        for (const auto &entry : next_configs) {
            const auto old_iter = previous_configs.find(entry.first);
            if (old_iter != previous_configs.end() &&
                old_iter->second.enabled == entry.second.enabled &&
                old_iter->second.dhcp == entry.second.dhcp &&
                old_iter->second.static_ipv4.address == entry.second.static_ipv4.address &&
                old_iter->second.static_ipv4.netmask == entry.second.static_ipv4.netmask &&
                old_iter->second.static_ipv4.gateway == entry.second.static_ipv4.gateway &&
                old_iter->second.dns == entry.second.dns) {
                continue;
            }
            if (!ApplyToPlatform(entry.second)) {
                for (auto iter = applied_ifnames.rbegin();
                     iter != applied_ifnames.rend(); ++iter) {
                    const auto previous_iter = previous_configs.find(*iter);
                    if (previous_iter != previous_configs.end()) {
                        static_cast<void>(
                            platform_->RollbackInterface(previous_iter->second));
                    }
                }
                const NetworkInterfaceConfig previous_config =
                    old_iter == previous_configs.end() ? DefaultConfig(entry.first)
                                                       : old_iter->second;
                static_cast<void>(platform_->RollbackInterface(previous_config));
                SetLastError(entry.first, false);
                return false;
            }
            SetLastError(entry.first, true);
            applied_ifnames.push_back(entry.first);
        }
        return true;
    }

    void SetLastError(const std::string &ifname, bool ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_errors_[ifname] = ok;
    }

    void PublishNetworkChanged(const std::string &ifname) {
        if (options_.event == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kNetworkChanged;
        event.source = NetworkConfig::Name();
        event.target = ifname;
        event.message = "interface_config_changed";
        static_cast<void>(options_.event->Publish(event));
    }

    void RecordAudit(const live_stream::RequestContext &context,
                     const std::string &ifname, bool ok, bool rejected,
                     const std::string &reason) {
        if (options_.logger == nullptr) {
            return;
        }
        OperationRecord record;
        record.timestamp_ms = infra::Time::SystemTimeMillis();
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = NetworkConfig::Name();
        record.action = OperationAction::kNetworkChange;
        record.target = ifname;
        record.result = ToOperationResult(ok, rejected);
        record.reason = reason;
        static_cast<void>(options_.logger->RecordOperation(record));
    }

    std::string AuditReason(const std::string &reason, bool rollback_ok) {
        std::string text = reason;
        if (!rollback_ok) {
            text += "; rollback_failed";
        }
        return text;
    }

    NetworkConfigOptions options_;
    std::unique_ptr<INetworkPlatform> owned_platform_;
    INetworkPlatform *platform_ = nullptr;
    std::map<std::string, NetworkInterfaceConfig> configs_;
    std::map<std::string, bool> status_errors_;
    ConfigJson network_config_json_ = ConfigJson::object();
    mutable std::mutex mutex_;
    bool config_attached_ = false;
    bool suppress_config_apply_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<INetworkConfig>
CreateNetworkConfig(const NetworkConfigOptions &options) {
    return std::unique_ptr<INetworkConfig>(new NetworkConfigImpl(options));
}

const char *NetworkConfig::Name() { return "network_config"; }

}  // namespace live_stream
