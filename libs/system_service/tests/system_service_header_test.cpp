#include "system_service.h"

#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeSystemPlatform : public live_stream::ISystemPlatform {
 public:
    infra::Result<live_stream::DeviceInfo> GetDeviceInfo() override {
        live_stream::DeviceInfo info;
        info.model = "ipc";
        info.serial_number = "sn-1";
        info.firmware_version = "0.1.0";
        return infra::Result<live_stream::DeviceInfo>::Ok(info);
    }

    infra::Result<live_stream::SystemStatus> GetSystemStatus() override {
        live_stream::SystemStatus status;
        status.cpu_usage_percent = 10;
        status.memory_usage_percent = 20;
        status.temperature_celsius = 40;
        status.uptime_ms = 1000;
        status.healthy = true;
        return infra::Result<live_stream::SystemStatus>::Ok(status);
    }

    infra::Result<live_stream::SystemCapabilities> GetCapabilities() override {
        live_stream::SystemCapabilities caps;
        caps.supports_reboot = true;
        caps.supports_factory_reset = true;
        caps.features.push_back("heartbeat");
        return infra::Result<live_stream::SystemCapabilities>::Ok(caps);
    }

    infra::Status Reboot() override {
        ++reboot_count;
        return reboot_error;
    }

    infra::Status FactoryReset() override {
        ++factory_reset_count;
        return factory_reset_error;
    }

    int reboot_count = 0;
    int factory_reset_count = 0;
    infra::Status reboot_error = infra::Status::kOk;
    infra::Status factory_reset_error = infra::Status::kOk;
};

class FakeEventService : public live_stream::IEventService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_event"; }

    infra::Result<live_stream::EventSubscriptionId> Subscribe(
        live_stream::EventType, live_stream::EventHandler) override {
        return infra::Result<live_stream::EventSubscriptionId>::Ok(1);
    }

    infra::Status Unsubscribe(live_stream::EventSubscriptionId) override {
        return infra::Status::kOk;
    }

    infra::Status Publish(const live_stream::Event& event) override {
        ++publish_count;
        last_event = event;
        return infra::Status::kOk;
    }

    int publish_count = 0;
    live_stream::Event last_event;
};

class FakeLoggerService : public live_stream::ILoggerService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_logger"; }

    infra::Status RecordOperation(
        const live_stream::OperationRecord& record) override {
        ++record_count;
        last_record = record;
        return infra::Status::kOk;
    }

    infra::Result<std::vector<live_stream::OperationRecord>> QueryOperations(
        const live_stream::OperationLogQuery&) override {
        return infra::Result<std::vector<live_stream::OperationRecord>>::Ok({});
    }

    infra::Status ExportOperations(
        const live_stream::OperationLogExportOptions&) override {
        return infra::Status::kOk;
    }

    int record_count = 0;
    live_stream::OperationRecord last_record;
};

}  // namespace

int main() {
    live_stream::SystemServiceOptions default_options;
    default_options.heartbeat_timeout_ms = 1;
    std::unique_ptr<live_stream::ISystemService> default_service =
        live_stream::CreateSystemService(default_options);
    infra::RequestContext default_context;
    if (!default_service ||
        default_service->Init() != infra::Status::kOk ||
        default_service->Start() != infra::Status::kOk ||
        default_service->Reboot(default_context) !=
            infra::Status::kNotSupported) {
        return 8;
    }
    default_service->Stop();
    default_service->Deinit();

    FakeSystemPlatform platform;
    FakeEventService event_service;
    FakeLoggerService logger_service;

    live_stream::SystemServiceOptions options;
    options.platform = &platform;
    options.event_service = &event_service;
    options.logger_service = &logger_service;
    options.heartbeat_timeout_ms = 1;
    std::unique_ptr<live_stream::ISystemService> service =
        live_stream::CreateSystemService(options);
    if (!service || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk ||
        std::string(service->Name()) != "system_service") {
        return 1;
    }

    infra::Result<live_stream::DeviceInfo> info = service->GetDeviceInfo();
    if (!info.IsOk() || info.value.model != "ipc") {
        return 2;
    }

    infra::Result<live_stream::SystemCapabilities> caps =
        service->GetCapabilities();
    if (!caps.IsOk() || caps.value.features.empty()) {
        return 3;
    }

    if (service->ReportHeartbeat("media") != infra::Status::kOk) {
        return 4;
    }
    infra::Time::SleepMillis(2);
    infra::Result<live_stream::SystemStatus> status =
        service->GetSystemStatus();
    if (!status.IsOk() || status.value.healthy ||
        event_service.last_event.type !=
            live_stream::EventType::kSystemStatusChanged) {
        return 5;
    }
    const int publish_count_after_timeout = event_service.publish_count;
    status = service->GetSystemStatus();
    if (!status.IsOk() ||
        event_service.publish_count != publish_count_after_timeout) {
        return 9;
    }
    if (service->ReportHeartbeat("media") != infra::Status::kOk) {
        return 10;
    }
    status = service->GetSystemStatus();
    if (!status.IsOk() || !status.value.healthy ||
        event_service.publish_count != publish_count_after_timeout + 1) {
        return 11;
    }

    infra::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    if (service->Reboot(context) != infra::Status::kOk ||
        platform.reboot_count != 1 ||
        logger_service.last_record.action != live_stream::OperationAction::kReboot) {
        return 6;
    }
    if (service->FactoryReset(context) != infra::Status::kOk ||
        platform.factory_reset_count != 1 ||
        logger_service.last_record.action !=
            live_stream::OperationAction::kFactoryReset) {
        return 7;
    }

    service->Stop();
    service->Deinit();
    return 0;
}
