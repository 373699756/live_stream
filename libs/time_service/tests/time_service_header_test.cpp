#include "time_service.h"

#include "event_service.h"
#include "logger_service.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeTimePlatform : public live_stream::ITimePlatform {
public:
    int64_t GetSystemTimeMs() override { return now_ms; }

    infra::Status SetSystemTimeMs(int64_t unix_time_ms) override {
        now_ms = unix_time_ms;
        ++set_count;
        return set_error;
    }

    infra::Status SyncNtp(const std::vector<std::string>& servers,
                          int64_t* synced_time_ms) override {
        ++ntp_count;
        last_servers = servers;
        if (synced_time_ms != nullptr) {
            *synced_time_ms = ntp_time_ms;
        }
        return ntp_error;
    }

    int64_t now_ms = 1000;
    int64_t ntp_time_ms = 2000;
    int set_count = 0;
    int ntp_count = 0;
    infra::Status set_error = infra::Status::kOk;
    infra::Status ntp_error = infra::Status::kOk;
    std::vector<std::string> last_servers;
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

std::unique_ptr<live_stream::ITimeService> CreateStartedService(
    FakeTimePlatform* platform,
    FakeEventService* event_service,
    FakeLoggerService* logger_service) {
    live_stream::TimeServiceOptions options;
    options.platform = platform;
    options.event_service = event_service;
    options.logger_service = logger_service;
    options.default_timezone = "Asia/Shanghai";
    options.default_ntp_config.enabled = true;
    options.default_ntp_config.servers.push_back("pool.ntp.org");
    options.default_ntp_config.sync_interval_sec = 3600;
    std::unique_ptr<live_stream::ITimeService> service =
        live_stream::CreateTimeService(options);
    if (!service || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    live_stream::TimeServiceOptions lifecycle_options;
    lifecycle_options.default_timezone = "UTC";
    lifecycle_options.default_ntp_config.enabled = false;
    std::unique_ptr<live_stream::ITimeService> lifecycle_service =
        live_stream::CreateTimeService(lifecycle_options);
    if (!lifecycle_service ||
        lifecycle_service->GetTimeStatus().status != infra::Status::kBusy ||
        lifecycle_service->Start() != infra::Status::kBusy) {
        return 10;
    }
    if (lifecycle_service->Init() != infra::Status::kOk ||
        lifecycle_service->SetTimezone(live_stream::RequestContext(), "UTC") !=
            infra::Status::kBusy ||
        lifecycle_service->Start() != infra::Status::kOk) {
        return 11;
    }
    lifecycle_service->Stop();
    if (lifecycle_service->SetSystemTime(live_stream::RequestContext(), 4000,
                                         live_stream::TimeSyncSource::kManual) !=
        infra::Status::kBusy) {
        return 12;
    }
    if (lifecycle_service->Start() != infra::Status::kOk ||
        lifecycle_service->GetTimeStatus().status != infra::Status::kOk) {
        return 13;
    }
    lifecycle_service->Deinit();

    live_stream::TimeServiceOptions invalid_options;
    invalid_options.default_timezone = "";
    if (live_stream::CreateTimeService(invalid_options)->Init() !=
        infra::Status::kInvalidParam) {
        return 14;
    }
    invalid_options.default_timezone = "UTC";
    invalid_options.default_ntp_config.enabled = true;
    invalid_options.default_ntp_config.sync_interval_sec = 3600;
    if (live_stream::CreateTimeService(invalid_options)->Init() !=
        infra::Status::kInvalidParam) {
        return 15;
    }

    FakeTimePlatform platform;
    FakeEventService event_service;
    FakeLoggerService logger_service;
    std::unique_ptr<live_stream::ITimeService> service =
        CreateStartedService(&platform, &event_service, &logger_service);
    if (!service || std::string(service->Name()) != "time_service") {
        return 1;
    }

    infra::Result<live_stream::TimeStatus> status = service->GetTimeStatus();
    if (!status.IsOk() || status.value.timezone != "Asia/Shanghai") {
        return 2;
    }

    live_stream::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    if (service->SetTimezone(context, "UTC") != infra::Status::kOk ||
        event_service.last_event.type != live_stream::EventType::kTimeChanged) {
        return 3;
    }

    if (service->SetSystemTime(context, 3000,
                               live_stream::TimeSyncSource::kManual) !=
            infra::Status::kOk ||
        platform.now_ms != 3000 || logger_service.record_count < 2) {
        return 4;
    }

    if (service->SyncNow(context, live_stream::TimeSyncSource::kNtp) !=
            infra::Status::kOk ||
        platform.now_ms != platform.ntp_time_ms || platform.ntp_count != 1) {
        return 5;
    }

    live_stream::NtpConfig disabled;
    disabled.enabled = false;
    disabled.sync_interval_sec = 3600;
    if (service->UpdateNtpConfig(context, disabled) != infra::Status::kOk ||
        service->SyncNow(context, live_stream::TimeSyncSource::kNtp) !=
            infra::Status::kInvalidParam) {
        return 6;
    }

    live_stream::NtpConfig invalid_enabled;
    invalid_enabled.enabled = true;
    invalid_enabled.sync_interval_sec = 3600;
    if (service->UpdateNtpConfig(context, invalid_enabled) !=
        infra::Status::kInvalidParam) {
        return 7;
    }

    live_stream::NtpConfig too_many_servers;
    too_many_servers.enabled = true;
    too_many_servers.sync_interval_sec = 3600;
    too_many_servers.servers = {"a", "b", "c", "d", "e"};
    if (service->UpdateNtpConfig(context, too_many_servers) !=
        infra::Status::kInvalidParam) {
        return 8;
    }

    const int64_t last_good_sync_time =
        service->GetTimeStatus().value.last_sync_time_ms;
    platform.set_error = infra::Status::kIoError;
    if (service->SetSystemTime(context, 5000,
                               live_stream::TimeSyncSource::kManual) !=
            infra::Status::kIoError ||
        service->GetTimeStatus().value.last_sync_error !=
            infra::Status::kIoError ||
        service->GetTimeStatus().value.last_sync_time_ms !=
            last_good_sync_time) {
        return 9;
    }
    platform.set_error = infra::Status::kOk;

    service->Stop();
    if (service->SetTimezone(context, "Asia/Tokyo") != infra::Status::kBusy ||
        service->Start() != infra::Status::kOk ||
        service->GetTimeStatus().value.timezone != "UTC") {
        return 16;
    }

    service->Stop();
    service->Deinit();
    return 0;
}
