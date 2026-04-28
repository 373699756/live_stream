#include "network_service.h"

#include "config_service.h"
#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include "nlohmann/json.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kConfigName = "network";
constexpr std::size_t kMaxIfnameLength = 15;
constexpr std::size_t kMaxDnsServers = 4;
using Json = nlohmann::json;

bool IsValidIpv4(const std::string& value) {
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

bool Ipv4ToUint32(const std::string& value, uint32_t* parsed) {
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

bool PrefixFromNetmask(const std::string& netmask, uint8_t* prefix_length) {
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
    const uint32_t mask = prefix_length == 32
                              ? 0xffffffffu
                              : (0xffffffffu << (32 - prefix_length));
    return std::to_string((mask >> 24) & 0xff) + "." +
           std::to_string((mask >> 16) & 0xff) + "." +
           std::to_string((mask >> 8) & 0xff) + "." +
           std::to_string(mask & 0xff);
}

bool IsValidIfname(const std::string& ifname) {
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

bool IsValidDnsServers(const std::vector<std::string>& dns_servers) {
    if (dns_servers.size() > kMaxDnsServers) {
        return false;
    }
    for (const std::string& dns : dns_servers) {
        if (!IsValidIpv4(dns)) {
            return false;
        }
    }
    return true;
}

infra::Status ValidateConfig(const NetworkInterfaceConfig& config,
                            bool allow_loopback_config) {
    if (!IsValidIfname(config.ifname)) {
        return infra::Status::kInvalidParam;
    }
    if (!allow_loopback_config && config.ifname == "lo") {
        return infra::Status::kNoPermission;
    }
    if (!IsValidDnsServers(config.dns_servers)) {
        return infra::Status::kInvalidParam;
    }
    if (!config.gateway.empty() && !IsValidIpv4(config.gateway)) {
        return infra::Status::kInvalidParam;
    }
    switch (config.address_mode) {
        case NetworkAddressMode::kDhcp:
            if (config.prefix_length > 32) {
                return infra::Status::kInvalidParam;
            }
            break;
        case NetworkAddressMode::kStatic:
            if (!IsValidIpv4(config.ipv4_address) ||
                config.prefix_length == 0 || config.prefix_length > 32) {
                return infra::Status::kInvalidParam;
            }
            break;
        default:
            return infra::Status::kInvalidParam;
    }
    return infra::Status::kOk;
}

OperationResult ToOperationResult(infra::Status error, bool rejected) {
    if (rejected) {
        return OperationResult::kRejected;
    }
    return error == infra::Status::kOk ? OperationResult::kSuccess
                                      : OperationResult::kFailed;
}

bool GetStringField(const Json& object,
                    const std::string& key,
                    std::string* value) {
    if (value == nullptr || !object.is_object() || !object.contains(key) ||
        !object.at(key).is_string()) {
        return false;
    }
    *value = object.at(key).get<std::string>();
    return true;
}

bool GetOptionalStringField(const Json& object,
                            const std::string& key,
                            std::string* value) {
    if (!object.is_object()) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    if (value == nullptr || !object.at(key).is_string()) {
        return false;
    }
    *value = object.at(key).get<std::string>();
    return true;
}

bool GetOptionalBoolField(const Json& object,
                          const std::string& key,
                          bool* value) {
    if (!object.is_object()) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    if (value == nullptr || !object.at(key).is_boolean()) {
        return false;
    }
    *value = object.at(key).get<bool>();
    return true;
}

bool GetOptionalUint8Field(const Json& object,
                           const std::string& key,
                           uint8_t* value) {
    if (!object.is_object()) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    if (value == nullptr || !object.at(key).is_number_integer()) {
        return false;
    }
    const int64_t parsed = object.at(key).get<int64_t>();
    if (parsed < 0 || parsed > 255) {
        return false;
    }
    *value = static_cast<uint8_t>(parsed);
    return true;
}

bool GetOptionalDnsField(const Json& object,
                         std::vector<std::string>* dns_servers) {
    if (!object.is_object()) {
        return false;
    }
    if (!object.contains("dns_servers")) {
        return true;
    }
    if (dns_servers == nullptr || !object.at("dns_servers").is_array()) {
        return false;
    }
    dns_servers->clear();
    for (const Json& item : object.at("dns_servers")) {
        if (!item.is_string()) {
            return false;
        }
        dns_servers->push_back(item.get<std::string>());
    }
    return true;
}

bool ParseMode(const std::string& mode, NetworkAddressMode* address_mode) {
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

bool ConfigFromJson(const Json& value,
                    NetworkInterfaceConfig* config) {
    if (!value.is_object() || config == nullptr) {
        return false;
    }
    NetworkInterfaceConfig parsed;
    std::string mode = "dhcp";
    if (!GetStringField(value, "ifname", &parsed.ifname) ||
        !GetOptionalBoolField(value, "enabled", &parsed.enabled) ||
        !GetOptionalStringField(value, "address_mode", &mode) ||
        !ParseMode(mode, &parsed.address_mode) ||
        !GetOptionalStringField(value, "ipv4_address", &parsed.ipv4_address) ||
        !GetOptionalUint8Field(value, "prefix_length", &parsed.prefix_length) ||
        !GetOptionalStringField(value, "gateway", &parsed.gateway) ||
        !GetOptionalDnsField(value, &parsed.dns_servers)) {
        return false;
    }
    *config = parsed;
    return true;
}

bool ConfigFromNetworkInterfaceJson(const std::string& ifname,
                                    const Json& value,
                                    NetworkInterfaceConfig* config) {
    if (!value.is_object() || config == nullptr) {
        return false;
    }
    NetworkInterfaceConfig parsed;
    parsed.ifname = ifname;
    if (!GetOptionalBoolField(value, "enabled", &parsed.enabled)) {
        return false;
    }
    bool dhcp = true;
    if (!GetOptionalBoolField(value, "dhcp", &dhcp)) {
        return false;
    }
    parsed.address_mode = dhcp ? NetworkAddressMode::kDhcp
                               : NetworkAddressMode::kStatic;
    if (value.contains("dns")) {
        if (!value.at("dns").is_array()) {
            return false;
        }
        parsed.dns_servers.clear();
        for (const Json& item : value.at("dns")) {
            if (!item.is_string()) {
                return false;
            }
            parsed.dns_servers.push_back(item.get<std::string>());
        }
    }
    if (value.contains("static_ipv4")) {
        const Json& static_ipv4 = value.at("static_ipv4");
        if (!static_ipv4.is_object()) {
            return false;
        }
        if (!GetOptionalStringField(static_ipv4, "address",
                                    &parsed.ipv4_address) ||
            !GetOptionalStringField(static_ipv4, "gateway",
                                    &parsed.gateway)) {
            return false;
        }
        std::string netmask;
        if (!GetOptionalStringField(static_ipv4, "netmask", &netmask)) {
            return false;
        }
        if (!netmask.empty() &&
            !PrefixFromNetmask(netmask, &parsed.prefix_length)) {
            return false;
        }
    }
    *config = parsed;
    return true;
}

Json ConfigToNetworkInterfaceJson(const NetworkInterfaceConfig& config) {
    Json value = Json::object();
    value["enabled"] = config.enabled;
    value["dhcp"] = config.address_mode == NetworkAddressMode::kDhcp;
    Json static_ipv4 = Json::object();
    static_ipv4["address"] = config.ipv4_address;
    static_ipv4["netmask"] = NetmaskFromPrefix(config.prefix_length);
    static_ipv4["gateway"] = config.gateway;
    value["static_ipv4"] = static_ipv4;
    Json dns = Json::array();
    for (const std::string& server : config.dns_servers) {
        dns.push_back(server);
    }
    value["dns"] = dns;
    return value;
}

bool ConfigsFromNetworkJson(
    const Json& json,
    std::map<std::string, NetworkInterfaceConfig>* configs) {
    if (configs == nullptr) {
        return false;
    }
    configs->clear();
    if (json.is_array()) {
        for (const Json& item : json) {
            NetworkInterfaceConfig config;
            if (!ConfigFromJson(item, &config) ||
                ValidateConfig(config, true) != infra::Status::kOk) {
                return false;
            }
            (*configs)[config.ifname] = config;
        }
        return true;
    }
    if (!json.is_object()) {
        return false;
    }
    if (!json.contains("interfaces")) {
        return true;
    }
    const Json& interfaces = json.at("interfaces");
    if (!interfaces.is_object()) {
        return false;
    }
    for (auto iter = interfaces.begin(); iter != interfaces.end(); ++iter) {
        NetworkInterfaceConfig config;
        if (!ConfigFromNetworkInterfaceJson(iter.key(), iter.value(), &config) ||
            ValidateConfig(config, true) != infra::Status::kOk) {
            return false;
        }
        (*configs)[config.ifname] = config;
    }
    return true;
}

Json NetworkJsonWithConfigs(
    const Json& current,
    const std::map<std::string, NetworkInterfaceConfig>& configs) {
    Json root = current.is_object() ? current : Json::object();
    Json interfaces = Json::object();
    for (const auto& entry : configs) {
        interfaces[entry.first] = ConfigToNetworkInterfaceJson(entry.second);
    }
    root["interfaces"] = interfaces;
    return root;
}

NetworkInterfaceConfig DefaultConfig(const std::string& ifname) {
    NetworkInterfaceConfig config;
    config.ifname = ifname.empty() ? "eth0" : ifname;
    config.enabled = true;
    config.address_mode = NetworkAddressMode::kDhcp;
    config.prefix_length = 24;
    return config;
}

class RestrictedNetworkPlatform : public INetworkPlatform {
 public:
    explicit RestrictedNetworkPlatform(std::string default_ifname)
        : default_ifname_(std::move(default_ifname)) {}

    infra::Result<std::vector<std::string>> ListInterfaces() override {
        return infra::Result<std::vector<std::string>>::Ok({default_ifname_});
    }

    infra::Result<NetworkInterfaceStatus> GetInterfaceStatus(
        const std::string& ifname) override {
        NetworkInterfaceStatus status;
        status.ifname = ifname;
        status.enabled = true;
        status.dhcp_enabled = true;
        status.last_error = infra::Status::kNotSupported;
        return infra::Result<NetworkInterfaceStatus>::Ok(status);
    }

    infra::Status ApplyStaticAddress(
        const NetworkInterfaceConfig&) override {
        return infra::Status::kNotSupported;
    }

    infra::Status SetInterfaceEnabled(const std::string&,
                                     bool) override {
        return infra::Status::kNotSupported;
    }

    infra::Status StartDhcp(const std::string&) override {
        return infra::Status::kNotSupported;
    }

    infra::Status StopDhcp(const std::string&) override {
        return infra::Status::kNotSupported;
    }

    infra::Status SetGateway(const std::string&,
                            const std::string&) override {
        return infra::Status::kNotSupported;
    }

    infra::Status SetDnsServers(
        const std::vector<std::string>&) override {
        return infra::Status::kNotSupported;
    }

    infra::Status RollbackInterface(
        const NetworkInterfaceConfig&) override {
        return infra::Status::kOk;
    }

 private:
    std::string default_ifname_;
};

class NetworkServiceImpl : public INetworkService {
 public:
    explicit NetworkServiceImpl(const NetworkServiceOptions& options)
        : options_(options),
          owned_platform_(options.platform == nullptr
                              ? new RestrictedNetworkPlatform(
                                    options.default_ifname.empty()
                                        ? "eth0"
                                        : options.default_ifname)
                              : nullptr),
          platform_(options.platform != nullptr ? options.platform
                                                : owned_platform_.get()) {}

    infra::Status Init() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) {
                return infra::Status::kOk;
            }
            if (platform_ == nullptr || options_.default_ifname.empty()) {
                return infra::Status::kInvalidParam;
            }
            configs_.clear();
        }
        const infra::Status load_error = LoadConfigs();
        if (load_error != infra::Status::kOk) {
            return load_error;
        }
        if (options_.config_service != nullptr && !config_callbacks_registered_) {
            infra::Status error = options_.config_service->RegisterVerify(
                kConfigName, [this](const ConfigJson& value) {
                    return VerifyNetworkConfig(value);
                });
            if (error != infra::Status::kOk) {
                return error;
            }
            error = options_.config_service->RegisterApply(
                kConfigName, [this](const ConfigJson& value) {
                    return ApplyNetworkConfig(value);
                });
            if (error != infra::Status::kOk) {
                return error;
            }
            config_callbacks_registered_ = true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return infra::Status::kBusy;
        }
        started_ = true;
        return infra::Status::kOk;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    void Deinit() override {
        std::lock_guard<std::mutex> lock(mutex_);
        status_errors_.clear();
        configs_.clear();
        suppress_config_apply_ = false;
        started_ = false;
        initialized_ = false;
    }

    const char* Name() const override { return NetworkService::Name(); }

    infra::Result<std::vector<std::string>> GetInterfaces() override {
        if (!IsStarted()) {
            return infra::Result<std::vector<std::string>>::Fail(
                infra::Status::kBusy);
        }
        return platform_->ListInterfaces();
    }

    infra::Result<NetworkInterfaceStatus> GetInterfaceStatus(
        const std::string& ifname) override {
        if (!IsValidIfname(ifname)) {
            return infra::Result<NetworkInterfaceStatus>::Fail(
                infra::Status::kInvalidParam);
        }
        if (!IsStarted()) {
            return infra::Result<NetworkInterfaceStatus>::Fail(
                infra::Status::kBusy);
        }
        infra::Result<NetworkInterfaceStatus> status =
            platform_->GetInterfaceStatus(ifname);
        if (!status.IsOk()) {
            return status;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iter = status_errors_.find(ifname);
        if (iter != status_errors_.end()) {
            status.value.last_error = iter->second;
        }
        return status;
    }

    infra::Status ApplyInterfaceConfig(
        const infra::RequestContext& context,
        const NetworkInterfaceConfig& config) override {
        const infra::Status validation =
            ValidateConfig(config, options_.allow_loopback_config);
        if (validation != infra::Status::kOk) {
            RecordAudit(context, config.ifname, validation, true,
                        infra::StatusToString(validation));
            return validation;
        }
        if (!IsStarted()) {
            RecordAudit(context, config.ifname, infra::Status::kBusy, false,
                        infra::StatusToString(infra::Status::kBusy));
            return infra::Status::kBusy;
        }

        NetworkInterfaceConfig previous_config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto iter = configs_.find(config.ifname);
            previous_config = iter == configs_.end()
                                  ? DefaultConfig(config.ifname)
                                  : iter->second;
        }
        const infra::Status apply_error = ApplyToPlatform(config);
        if (apply_error != infra::Status::kOk) {
            const infra::Status rollback_error =
                platform_->RollbackInterface(previous_config);
            SetLastError(config.ifname, apply_error);
            RecordAudit(context, config.ifname, apply_error, false,
                        AuditReason(apply_error, rollback_error));
            return apply_error;
        }

        const infra::Status persist_error = PersistConfig(config);
        if (persist_error != infra::Status::kOk) {
            const infra::Status rollback_error =
                platform_->RollbackInterface(previous_config);
            SetLastError(config.ifname, persist_error);
            RecordAudit(context, config.ifname, persist_error, false,
                        AuditReason(persist_error, rollback_error));
            return persist_error;
        }

        SetLastError(config.ifname, infra::Status::kOk);
        PublishNetworkChanged(config.ifname);
        RecordAudit(context, config.ifname, infra::Status::kOk, false,
                    infra::StatusToString(infra::Status::kOk));
        return infra::Status::kOk;
    }

    infra::Status ReloadStatus() override {
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        infra::Result<std::vector<std::string>> interfaces =
            platform_->ListInterfaces();
        if (!interfaces.IsOk()) {
            return interfaces.status;
        }
        for (const std::string& ifname : interfaces.value) {
            infra::Result<NetworkInterfaceStatus> status =
                platform_->GetInterfaceStatus(ifname);
            SetLastError(ifname, status.IsOk() ? status.value.last_error
                                               : status.status);
        }
        return infra::Status::kOk;
    }

 private:
    bool IsStarted() {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    infra::Status LoadConfigs() {
        Json network_json = Json::object();
        std::map<std::string, NetworkInterfaceConfig> loaded_configs;
        if (options_.config_service == nullptr) {
            NetworkInterfaceConfig config = DefaultConfig(options_.default_ifname);
            loaded_configs[config.ifname] = config;
            network_json = NetworkJsonWithConfigs(network_json, loaded_configs);
        } else {
            ConfigJson json;
            const infra::Status get_error =
                options_.config_service->GetValue(kConfigName, &json);
            if (get_error == infra::Status::kNotFound) {
                NetworkInterfaceConfig config =
                    DefaultConfig(options_.default_ifname);
                loaded_configs[config.ifname] = config;
                network_json = NetworkJsonWithConfigs(network_json, loaded_configs);
            } else if (get_error != infra::Status::kOk) {
                return get_error;
            } else if (!ConfigsFromNetworkJson(json, &loaded_configs)) {
                return infra::Status::kInvalidParam;
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
        return infra::Status::kOk;
    }

    infra::Status ApplyToPlatform(const NetworkInterfaceConfig& config) {
        infra::Status error =
            platform_->SetInterfaceEnabled(config.ifname, config.enabled);
        if (error != infra::Status::kOk || !config.enabled) {
            return error;
        }
        switch (config.address_mode) {
            case NetworkAddressMode::kDhcp:
                error = platform_->StartDhcp(config.ifname);
                break;
            case NetworkAddressMode::kStatic:
                error = platform_->StopDhcp(config.ifname);
                if (error == infra::Status::kOk) {
                    error = platform_->ApplyStaticAddress(config);
                }
                break;
            default:
                return infra::Status::kInvalidParam;
        }
        if (error == infra::Status::kOk) {
            error = platform_->SetGateway(config.ifname, config.gateway);
        }
        if (error == infra::Status::kOk) {
            error = platform_->SetDnsServers(config.dns_servers);
        }
        return error;
    }

    infra::Status PersistConfig(const NetworkInterfaceConfig& config) {
        std::map<std::string, NetworkInterfaceConfig> next_configs;
        Json next_json;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_configs = configs_;
            next_configs[config.ifname] = config;
            next_json = network_config_json_.is_object()
                            ? network_config_json_
                            : Json::object();
        }
        next_json = NetworkJsonWithConfigs(next_json, next_configs);
        if (options_.config_service != nullptr) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                suppress_config_apply_ = true;
            }
            const infra::Status error = options_.config_service->SetValue(
                kConfigName, next_json);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                suppress_config_apply_ = false;
            }
            if (error != infra::Status::kOk) {
                return error;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        configs_ = next_configs;
        network_config_json_ = next_json;
        return infra::Status::kOk;
    }

    infra::Status VerifyNetworkConfig(const Json& json) {
        std::map<std::string, NetworkInterfaceConfig> parsed;
        if (!ConfigsFromNetworkJson(json, &parsed)) {
            return infra::Status::kInvalidParam;
        }
        for (const auto& entry : parsed) {
            const infra::Status status =
                ValidateConfig(entry.second, options_.allow_loopback_config);
            if (status != infra::Status::kOk) {
                return status;
            }
        }
        return infra::Status::kOk;
    }

    infra::Status ApplyNetworkConfig(const Json& json) {
        std::map<std::string, NetworkInterfaceConfig> parsed;
        if (!ConfigsFromNetworkJson(json, &parsed)) {
            return infra::Status::kInvalidParam;
        }
        if (parsed.empty()) {
            NetworkInterfaceConfig config = DefaultConfig(options_.default_ifname);
            parsed[config.ifname] = config;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (suppress_config_apply_) {
                return infra::Status::kOk;
            }
        }
        const infra::Status error = ApplyConfigChanges(parsed);
        if (error != infra::Status::kOk) {
            return error;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        configs_ = parsed;
        network_config_json_ = NetworkJsonWithConfigs(json, parsed);
        return infra::Status::kOk;
    }

    infra::Status ApplyConfigChanges(
        const std::map<std::string, NetworkInterfaceConfig>& next_configs) {
        std::map<std::string, NetworkInterfaceConfig> previous_configs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previous_configs = configs_;
        }
        std::vector<std::string> applied_ifnames;
        for (const auto& entry : next_configs) {
            const auto old_iter = previous_configs.find(entry.first);
            if (old_iter != previous_configs.end() &&
                old_iter->second.enabled == entry.second.enabled &&
                old_iter->second.address_mode == entry.second.address_mode &&
                old_iter->second.ipv4_address == entry.second.ipv4_address &&
                old_iter->second.prefix_length == entry.second.prefix_length &&
                old_iter->second.gateway == entry.second.gateway &&
                old_iter->second.dns_servers == entry.second.dns_servers) {
                continue;
            }
            const infra::Status error = ApplyToPlatform(entry.second);
            if (error != infra::Status::kOk) {
                for (auto iter = applied_ifnames.rbegin();
                     iter != applied_ifnames.rend(); ++iter) {
                    const auto previous_iter = previous_configs.find(*iter);
                    if (previous_iter != previous_configs.end()) {
                        static_cast<void>(
                            platform_->RollbackInterface(previous_iter->second));
                    }
                }
                const NetworkInterfaceConfig previous_config =
                    old_iter == previous_configs.end()
                        ? DefaultConfig(entry.first)
                        : old_iter->second;
                static_cast<void>(platform_->RollbackInterface(previous_config));
                SetLastError(entry.first, error);
                return error;
            }
            SetLastError(entry.first, infra::Status::kOk);
            applied_ifnames.push_back(entry.first);
        }
        return infra::Status::kOk;
    }

    void SetLastError(const std::string& ifname, infra::Status error) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_errors_[ifname] = error;
    }

    void PublishNetworkChanged(const std::string& ifname) {
        if (options_.event_service == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kNetworkChanged;
        event.source = NetworkService::Name();
        event.target = ifname;
        event.message = "interface_config_changed";
        static_cast<void>(options_.event_service->Publish(event));
    }

    void RecordAudit(const infra::RequestContext& context,
                     const std::string& ifname,
                     infra::Status error,
                     bool rejected,
                     const std::string& reason) {
        if (options_.logger_service == nullptr) {
            return;
        }
        OperationRecord record;
        record.timestamp_ms = infra::Time::SystemTimeMillis();
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = NetworkService::Name();
        record.action = OperationAction::kNetworkChange;
        record.target = ifname;
        record.result = ToOperationResult(error, rejected);
        record.reason = reason;
        static_cast<void>(options_.logger_service->RecordOperation(record));
    }

    std::string AuditReason(infra::Status error, infra::Status rollback_error) {
        std::string reason = infra::StatusToString(error);
        if (rollback_error != infra::Status::kOk) {
            reason += "; rollback=";
            reason += infra::StatusToString(rollback_error);
        }
        return reason;
    }

    NetworkServiceOptions options_;
    std::unique_ptr<INetworkPlatform> owned_platform_;
    INetworkPlatform* platform_ = nullptr;
    std::map<std::string, NetworkInterfaceConfig> configs_;
    std::map<std::string, infra::Status> status_errors_;
    Json network_config_json_ = Json::object();
    std::mutex mutex_;
    bool config_callbacks_registered_ = false;
    bool suppress_config_apply_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<INetworkService> CreateNetworkService(
    const NetworkServiceOptions& options) {
    return std::unique_ptr<INetworkService>(new NetworkServiceImpl(options));
}

const char* NetworkAddressModeToString(NetworkAddressMode mode) {
    switch (mode) {
        case NetworkAddressMode::kDhcp:
            return "dhcp";
        case NetworkAddressMode::kStatic:
            return "static";
    }
    return "unknown";
}

const char* NetworkService::Name() {
    return "network_service";
}

}  // namespace live_stream
