#include "system.h"

#include "event.h"
#include "infra/time.h"
#include "logger.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeSystemPlatform : public live_stream::ISystemPlatform {
public:
    live_stream::DeviceInfo GetDeviceInfo() override {
        live_stream::DeviceInfo info;
        info.model = "ipc";
        info.serial_number = "sn-1";
        info.firmware_version = "0.1.0";
        return info;
    }

    live_stream::SystemStatus GetSystemStatus() override {
        live_stream::SystemStatus status;
        status.cpu_usage_percent = 10;
        status.memory_usage_percent = 20;
        status.temperature_celsius = 40;
        status.uptime_ms = 1000;
        status.healthy = true;
        return status;
    }

    live_stream::SystemCapabilities GetCapabilities() override {
        live_stream::SystemCapabilities caps;
        caps.supports_reboot = true;
        caps.supports_factory_reset = true;
        caps.features.push_back("heartbeat");
        return caps;
    }

    bool Reboot() override {
        ++reboot_count;
        return reboot_error;
    }

    bool FactoryReset() override {
        ++factory_reset_count;
        return factory_reset_error;
    }

    int reboot_count = 0;
    int factory_reset_count = 0;
    bool reboot_error = true;
    bool factory_reset_error = true;
};

class FakeEvent : public live_stream::event::Dispatcher {
public:
    FakeEvent()
        : subscription_(Subscribe(
              live_stream::event::EventType::kSystemStatusChanged,
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

}  // namespace

int main() {
    live_stream::SystemOptions default_options;
    default_options.heartbeat_timeout_ms = 1;
    std::unique_ptr<live_stream::ISystem> default_system =
        live_stream::CreateSystem(default_options);
    live_stream::RequestContext default_context;
    if (!default_system ||
        !default_system->Start() ||
        default_system->Reboot(default_context)) {
        return 8;
    }
    default_system->Stop();

    FakeSystemPlatform platform;
    FakeEvent event;
    FakeLogger logger;

    live_stream::SystemOptions options;
    options.platform = &platform;
    options.event = &event;
    options.logger = &logger;
    options.heartbeat_timeout_ms = 1;
    std::unique_ptr<live_stream::ISystem> service =
        live_stream::CreateSystem(options);
    if (!service || !service->Start()) {
        return 1;
    }

    live_stream::DeviceInfo info = service->GetDeviceInfo();
    if (info.model != "ipc") {
        return 2;
    }

    live_stream::SystemCapabilities caps = service->GetCapabilities();
    if (caps.features.empty()) {
        return 3;
    }

    if (!service->ReportHeartbeat("media")) {
        return 4;
    }
    infra::Time::SleepMillis(2);
    live_stream::SystemStatus status = service->GetSystemStatus();
    if (status.healthy ||
        event.last_event.type !=
            live_stream::event::EventType::kSystemStatusChanged) {
        return 5;
    }
    const int publish_count_after_timeout = event.publish_count;
    status = service->GetSystemStatus();
    if (event.publish_count != publish_count_after_timeout) {
        return 9;
    }
    if (!service->ReportHeartbeat("media")) {
        return 10;
    }
    status = service->GetSystemStatus();
    if (!status.healthy ||
        event.publish_count != publish_count_after_timeout + 1) {
        return 11;
    }

    live_stream::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    if (!service->Reboot(context) ||
        platform.reboot_count != 1 ||
        logger.last_record.action != live_stream::OperationAction::kReboot) {
        return 6;
    }
    if (!service->FactoryReset(context) ||
        platform.factory_reset_count != 1 ||
        logger.last_record.action !=
            live_stream::OperationAction::kFactoryReset) {
        return 7;
    }

    service->Stop();
    return 0;
}
