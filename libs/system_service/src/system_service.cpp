#include "system_service.h"

#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include <map>
#include <mutex>
#include <utility>

namespace live_stream {
namespace {

constexpr std::size_t kMaxComponentNameLength = 64;

OperationResult ToOperationResult(infra::Status error) {
    return error == infra::Status::kOk ? OperationResult::kSuccess
                                      : OperationResult::kFailed;
}

class SystemServiceImpl : public ISystemService {
 public:
    explicit SystemServiceImpl(const SystemServiceOptions& options)
        : options_(options) {}

    infra::Status Init() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (options_.platform == nullptr || options_.heartbeat_timeout_ms == 0) {
            return infra::Status::kInvalidParam;
        }
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
        heartbeat_ms_.clear();
        started_ = false;
        initialized_ = false;
    }

    const char* Name() const override { return "system_service"; }

    infra::Result<DeviceInfo> GetDeviceInfo() override {
        if (!IsStarted()) {
            return infra::Result<DeviceInfo>::Fail(infra::Status::kBusy);
        }
        return options_.platform->GetDeviceInfo();
    }

    infra::Result<SystemStatus> GetSystemStatus() override {
        if (!IsStarted()) {
            return infra::Result<SystemStatus>::Fail(infra::Status::kBusy);
        }
        infra::Result<SystemStatus> status = options_.platform->GetSystemStatus();
        if (!status.IsOk()) {
            return status;
        }
        ApplyHeartbeatHealth(&status.value);
        return status;
    }

    infra::Result<SystemCapabilities> GetCapabilities() override {
        if (!IsStarted()) {
            return infra::Result<SystemCapabilities>::Fail(infra::Status::kBusy);
        }
        return options_.platform->GetCapabilities();
    }

    infra::Status Reboot(const infra::RequestContext& context) override {
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        const infra::Status error = options_.platform->Reboot();
        RecordAudit(context, OperationAction::kReboot, error, "system");
        if (error == infra::Status::kOk) {
            PublishSystemStatusChanged("reboot");
        }
        return error;
    }

    infra::Status FactoryReset(const infra::RequestContext& context) override {
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        const infra::Status error = options_.platform->FactoryReset();
        RecordAudit(context, OperationAction::kFactoryReset, error, "system");
        if (error == infra::Status::kOk) {
            PublishSystemStatusChanged("factory_reset");
        }
        return error;
    }

    infra::Status ReportHeartbeat(const std::string& component) override {
        if (component.empty() || component.size() > kMaxComponentNameLength) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            heartbeat_ms_[component] = infra::Time::MonotonicMillis();
        }
        return infra::Status::kOk;
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
        }
        if (!failed_component.empty()) {
            status->healthy = false;
            status->health_reason = "heartbeat timeout: " + failed_component;
            PublishSystemStatusChanged(status->health_reason);
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

    void RecordAudit(const infra::RequestContext& context,
                     OperationAction action,
                     infra::Status error,
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
        record.result = ToOperationResult(error);
        record.reason = infra::StatusToString(error);
        static_cast<void>(options_.logger_service->RecordOperation(record));
    }

    SystemServiceOptions options_;
    std::map<std::string, int64_t> heartbeat_ms_;
    std::mutex mutex_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ISystemService> CreateSystemService(
    const SystemServiceOptions& options) {
    return std::unique_ptr<ISystemService>(new SystemServiceImpl(options));
}

}  // namespace live_stream
