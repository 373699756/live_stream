#include "system_service.h"

#include "event_service.h"
#include "infra/fs.h"
#include "infra/time.h"
#include "logger_service.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

namespace live_stream {
namespace {

constexpr std::size_t kMaxComponentNameLength = 64;
constexpr std::size_t kMaxHeartbeatComponents = 32;

OperationResult ToOperationResult(bool ok) {
    return ok ? OperationResult::kSuccess : OperationResult::kFailed;
}

bool ParseFirstInteger(const std::string& text, int64_t* value) {
    if (value == nullptr) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        return false;
    }
    *value = parsed;
    return true;
}

int64_t ReadUptimeMs() {
    std::string content = infra::File::ReadAll("/proc/uptime");
    if (content.empty()) {
        return 0;
    }
    std::istringstream stream(content);
    double uptime_sec = 0.0;
    stream >> uptime_sec;
    if (!stream) {
        return 0;
    }
    return static_cast<int64_t>(uptime_sec * 1000.0);
}

uint32_t ReadMemoryUsagePercent() {
    std::string content = infra::File::ReadAll("/proc/meminfo");
    if (content.empty()) {
        return 0;
    }
    std::istringstream stream(content);
    std::string key;
    int64_t value = 0;
    std::string unit;
    int64_t total_kb = 0;
    int64_t available_kb = 0;
    while (stream >> key >> value >> unit) {
        if (key == "MemTotal:") {
            total_kb = value;
        } else if (key == "MemAvailable:") {
            available_kb = value;
        }
    }
    if (total_kb <= 0 || available_kb < 0 || available_kb > total_kb) {
        return 0;
    }
    return static_cast<uint32_t>((total_kb - available_kb) * 100 / total_kb);
}

int32_t ReadTemperatureCelsius() {
    std::string content =
        infra::File::ReadAll("/sys/class/thermal/thermal_zone0/temp");
    int64_t raw = 0;
    if (content.empty() || !ParseFirstInteger(content, &raw)) {
        return 0;
    }
    if (raw > 1000) {
        raw /= 1000;
    }
    return static_cast<int32_t>(raw);
}

class DefaultSystemPlatform : public ISystemPlatform {
public:
    DeviceInfo GetDeviceInfo() override {
        DeviceInfo info;
        info.model = "live_stream_ipc";
        info.serial_number = "unknown";
        info.firmware_version = "0.1.0";
        return info;
    }

    SystemStatus GetSystemStatus() override {
        SystemStatus status;
        status.cpu_usage_percent = 0;
        status.memory_usage_percent = ReadMemoryUsagePercent();
        status.temperature_celsius = ReadTemperatureCelsius();
        status.uptime_ms = ReadUptimeMs();
        status.healthy = true;
        return status;
    }

    SystemCapabilities GetCapabilities() override {
        SystemCapabilities caps;
        caps.supports_reboot = false;
        caps.supports_factory_reset = false;
        caps.features.push_back("heartbeat");
        return caps;
    }

    bool Reboot() override { return false; }

    bool FactoryReset() override {
        return false;
    }
};

class SystemServiceImpl : public ISystemService {
public:
    explicit SystemServiceImpl(const SystemServiceOptions& options)
        : options_(options),
          owned_platform_(options.platform == nullptr
                              ? new DefaultSystemPlatform()
                              : nullptr),
          platform_(options.platform != nullptr ? options.platform
                                                : owned_platform_.get()) {}

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        if (platform_ == nullptr || options_.heartbeat_timeout_ms == 0) {
            return false;
        }
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

    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        heartbeat_ms_.clear();
        heartbeat_unhealthy_ = false;
        last_failed_component_.clear();
        started_ = false;
        initialized_ = false;
    }

    DeviceInfo GetDeviceInfo() override {
        if (!IsStarted()) {
            return DeviceInfo{};
        }
        return platform_->GetDeviceInfo();
    }

    SystemStatus GetSystemStatus() override {
        if (!IsStarted()) {
            return SystemStatus{};
        }
        SystemStatus status = platform_->GetSystemStatus();
        ApplyHeartbeatHealth(&status);
        return status;
    }

    SystemCapabilities GetCapabilities() override {
        if (!IsStarted()) {
            return SystemCapabilities{};
        }
        return platform_->GetCapabilities();
    }

    bool Reboot(const live_stream::RequestContext& context) override {
        if (!IsStarted()) {
            return false;
        }
        const bool ok = platform_->Reboot();
        RecordAudit(context, OperationAction::kReboot, ok, "system");
        if (ok) {
            PublishSystemStatusChanged("reboot");
        }
        return ok;
    }

    bool FactoryReset(const live_stream::RequestContext& context) override {
        if (!IsStarted()) {
            return false;
        }
        const bool ok = platform_->FactoryReset();
        RecordAudit(context, OperationAction::kFactoryReset, ok, "system");
        if (ok) {
            PublishSystemStatusChanged("factory_reset");
        }
        return ok;
    }

    bool ReportHeartbeat(const std::string& component) override {
        if (component.empty() || component.size() > kMaxComponentNameLength) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (heartbeat_ms_.find(component) == heartbeat_ms_.end() &&
                heartbeat_ms_.size() >= kMaxHeartbeatComponents) {
                return false;
            }
            heartbeat_ms_[component] = infra::Time::MonotonicMillis();
        }
        return true;
    }

private:
    bool IsStarted() {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void ApplyHeartbeatHealth(SystemStatus* status) {
        if (status == nullptr) {
            return;
        }
        std::string failed_component;
        std::string event_message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int64_t now = infra::Time::MonotonicMillis();
            for (const auto& entry : heartbeat_ms_) {
                if (now - entry.second >
                    static_cast<int64_t>(options_.heartbeat_timeout_ms)) {
                    failed_component = entry.first;
                    break;
                }
            }
            if (!failed_component.empty()) {
                if (!heartbeat_unhealthy_ ||
                    failed_component != last_failed_component_) {
                    event_message = "heartbeat timeout: " + failed_component;
                }
                heartbeat_unhealthy_ = true;
                last_failed_component_ = failed_component;
            } else if (heartbeat_unhealthy_) {
                event_message = "heartbeat recovered";
                heartbeat_unhealthy_ = false;
                last_failed_component_.clear();
            }
        }
        if (!failed_component.empty()) {
            status->healthy = false;
            status->health_reason = "heartbeat timeout: " + failed_component;
        }
        if (!event_message.empty()) {
            PublishSystemStatusChanged(event_message);
        }
    }

    void PublishSystemStatusChanged(const std::string& message) {
        if (options_.event_service == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kSystemStatusChanged;
        event.source = "system_service";
        event.message = message;
        static_cast<void>(options_.event_service->Publish(event));
    }

    void RecordAudit(const live_stream::RequestContext& context,
                     OperationAction action,
                     bool ok,
                     const std::string& target) {
        if (options_.logger_service == nullptr) {
            return;
        }
        OperationRecord record;
        record.timestamp_ms = infra::Time::SystemTimeMillis();
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = "system_service";
        record.action = action;
        record.target = target;
        record.result = ToOperationResult(ok);
        record.reason = ok ? "ok" : "failed";
        static_cast<void>(options_.logger_service->RecordOperation(record));
    }

    SystemServiceOptions options_;
    std::unique_ptr<ISystemPlatform> owned_platform_;
    ISystemPlatform* platform_ = nullptr;
    std::map<std::string, int64_t> heartbeat_ms_;
    std::mutex mutex_;
    bool heartbeat_unhealthy_ = false;
    std::string last_failed_component_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ISystemService> CreateSystemService(
    const SystemServiceOptions& options) {
    return std::unique_ptr<ISystemService>(new SystemServiceImpl(options));
}

}  // namespace live_stream
