#include "time_api.h"

#include "config.h"
#include "event.h"
#include "logger.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeTimePlatform : public live_stream::ITimePlatform {
public:
    int64_t GetSystemTimeMs() override { return now_ms; }

    bool SetSystemTimeMs(int64_t unix_time_ms) override {
        ++set_count;
        if (!set_ok) {
            return false;
        }
        now_ms = unix_time_ms;
        return true;
    }

    bool SyncNtp(const std::vector<std::string>& servers,
                 int64_t* synced_time_ms) override {
        ++ntp_count;
        last_servers = servers;
        if (synced_time_ms != nullptr) {
            *synced_time_ms = ntp_time_ms;
        }
        return ntp_ok;
    }

    int64_t now_ms = 1000;
    int64_t ntp_time_ms = 2000;
    int set_count = 0;
    int ntp_count = 0;
    bool set_ok = true;
    bool ntp_ok = true;
    std::vector<std::string> last_servers;
};

class FakeEvent : public live_stream::event::Dispatcher {
public:
    FakeEvent()
        : subscription_(Subscribe(
              live_stream::event::EventType::kTimeChanged,
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

class FakeConfig : public live_stream::IConfig {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::ConfigStatus Set(const std::string& name,
                                  const live_stream::ConfigJson& now,
                                  live_stream::ConfigIssue* issue) override {
        if (!set_ok) {
            return live_stream::ConfigStatus::kSaveFailed;
        }
        if (name == scope_name && scope.verify) {
            const live_stream::ConfigStatus status = scope.verify(now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        if (name == scope_name && scope.apply) {
            const live_stream::ConfigStatus status =
                scope.apply(value_json, now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        value_name = name;
        value_json = now;
        return live_stream::ConfigStatus::kOk;
    }

    live_stream::ConfigJson Get(const std::string& name) override {
        return name == value_name ? value_json : live_stream::ConfigJson();
    }

    live_stream::ConfigStatus Reset(
        const std::string&, live_stream::ConfigIssue*) override {
        return live_stream::ConfigStatus::kNotFound;
    }

    live_stream::ConfigJson Default(const std::string&) override {
        return live_stream::ConfigJson();
    }

    live_stream::ConfigStatus ResetAll(
        live_stream::ConfigIssue*) override {
        return live_stream::ConfigStatus::kNotFound;
    }

    bool AddScope(const std::string& name,
                  const live_stream::ConfigScope& next_scope) override {
        scope_name = name;
        scope = next_scope;
        return true;
    }

    bool RemoveScope(const std::string&) override { return true; }

    std::string value_name;
    std::string scope_name;
    live_stream::ConfigJson value_json;
    live_stream::ConfigScope scope;
    bool set_ok = true;
};

std::unique_ptr<live_stream::ITime> CreateStarted(
    FakeTimePlatform* platform,
    FakeEvent* event,
    FakeLogger* logger) {
    live_stream::TimeOptions options;
    options.platform = platform;
    options.event = event;
    options.logger = logger;
    options.default_timezone = "Asia/Shanghai";
    options.default_ntp_config.enabled = true;
    options.default_ntp_config.servers.push_back("pool.ntp.org");
    options.default_ntp_config.sync_interval_sec = 3600;
    std::unique_ptr<live_stream::ITime> service =
        live_stream::CreateTime(options);
    if (!service || !service->Start()) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    if (std::string(live_stream::TimeSyncSourceToString(
            live_stream::TimeSyncSource::kNtp)) != "ntp") {
        return 1;
    }
    if (std::string(live_stream::TimeSyncSourceToString(
            live_stream::TimeSyncSource::kBrowser)) != "browser") {
        return 17;
    }

    live_stream::TimeOptions start_stop_options;
    start_stop_options.default_timezone = "UTC";
    start_stop_options.default_ntp_config.enabled = false;
    std::unique_ptr<live_stream::ITime> start_stop_time =
        live_stream::CreateTime(start_stop_options);
    if (!start_stop_time || start_stop_time->IsStarted()) {
        return 2;
    }
    if (!start_stop_time->Start() || !start_stop_time->IsStarted()) {
        return 3;
    }
    start_stop_time->Stop();
    if (start_stop_time->SetSystemTime(live_stream::RequestContext(), 4000,
                                      live_stream::TimeSyncSource::kManual)) {
        return 4;
    }

    live_stream::TimeOptions invalid_options;
    invalid_options.default_timezone = "";
    if (live_stream::CreateTime(invalid_options)->Start()) {
        return 5;
    }
    invalid_options.default_timezone = "UTC";
    invalid_options.default_ntp_config.enabled = true;
    invalid_options.default_ntp_config.sync_interval_sec = 3600;
    if (live_stream::CreateTime(invalid_options)->Start()) {
        return 6;
    }

    FakeTimePlatform platform;
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::ITime> service =
        CreateStarted(&platform, &event, &logger);
    if (!service) {
        return 7;
    }

    live_stream::TimeStatus status = service->GetTimeStatus();
    if (status.timezone != "Asia/Shanghai" ||
        status.system_time_ms != platform.now_ms) {
        return 8;
    }

    live_stream::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    if (!service->SetTimezone(context, "UTC") ||
        event.last_event.type != live_stream::event::EventType::kTimeChanged ||
        service->GetTimeStatus().timezone != "UTC") {
        return 9;
    }

    if (!service->SetSystemTime(context, 3000,
                                live_stream::TimeSyncSource::kManual) ||
        platform.now_ms != 3000 ||
        logger.last_record.result !=
            live_stream::OperationResult::kSuccess) {
        return 10;
    }

    if (!service->SetSystemTime(context, 3500,
                                live_stream::TimeSyncSource::kBrowser) ||
        platform.now_ms != 3500 ||
        service->GetTimeStatus().last_sync_source !=
            live_stream::TimeSyncSource::kBrowser) {
        return 18;
    }

    if (!service->UpdateBrowserSyncConfig(context, true, false) ||
        !service->GetTimeStatus().manual_sync_allowed ||
        service->GetTimeStatus().browser_sync_on_login) {
        return 19;
    }

    if (!service->SetSystemTime(context, 3600,
                                live_stream::TimeSyncSource::kBrowser) ||
        platform.now_ms != 3600) {
        return 20;
    }

    if (!service->UpdateBrowserSyncConfig(context, false, false) ||
        service->SetSystemTime(context, 3700,
                               live_stream::TimeSyncSource::kBrowser)) {
        return 21;
    }

    if (!service->UpdateBrowserSyncConfig(context, true, true)) {
        return 22;
    }

    live_stream::TimeConfig config;
    config.timezone = "Asia/Tokyo";
    config.ntp.enabled = false;
    config.ntp.sync_interval_sec = 3600;
    config.manual_sync_allowed = false;
    config.browser_sync_on_login = true;
    if (!service->UpdateTimeConfig(context, config) ||
        service->GetTimeStatus().timezone != "Asia/Tokyo" ||
        service->GetTimeStatus().manual_sync_allowed ||
        service->GetTimeStatus().browser_sync_on_login) {
        return 23;
    }

    FakeConfig stored_config;
    live_stream::TimeOptions stored_options;
    stored_options.config = &stored_config;
    stored_options.default_timezone = "UTC";
    stored_options.default_ntp_config.enabled = false;
    stored_options.default_ntp_config.sync_interval_sec = 3600;
    std::unique_ptr<live_stream::ITime> stored_service =
        live_stream::CreateTime(stored_options);
    if (!stored_service || !stored_service->Start()) {
        return 24;
    }
    config.timezone = "Asia/Tokyo";
    config.ntp.enabled = false;
    config.ntp.servers.clear();
    config.ntp.sync_interval_sec = 3600;
    config.manual_sync_allowed = false;
    config.browser_sync_on_login = true;
    if (!stored_service->UpdateTimeConfig(context, config) ||
        stored_config.value_name != "time" ||
        stored_config.value_json["browser_sync_on_login"].get<bool>()) {
        return 25;
    }
    live_stream::ConfigJson external_config = stored_config.value_json;
    external_config["manual_sync_allowed"] = false;
    external_config["browser_sync_on_login"] = true;
    if (stored_config.Set("time", external_config, nullptr) !=
            live_stream::ConfigStatus::kOk ||
        stored_service->GetTimeStatus().browser_sync_on_login) {
        return 26;
    }
    if (!stored_service->UpdateBrowserSyncConfig(context, false, true) ||
        stored_config.value_json["browser_sync_on_login"].get<bool>()) {
        return 27;
    }

    config.timezone = "UTC";
    config.ntp.enabled = true;
    config.ntp.servers = {"pool.ntp.org"};
    config.manual_sync_allowed = true;
    config.browser_sync_on_login = true;
    if (!service->UpdateTimeConfig(context, config)) {
        return 28;
    }

    if (!service->SyncNow(context, live_stream::TimeSyncSource::kNtp) ||
        platform.now_ms != platform.ntp_time_ms || platform.ntp_count != 1 ||
        platform.last_servers.empty()) {
        return 11;
    }

    live_stream::NtpConfig disabled;
    disabled.enabled = false;
    disabled.sync_interval_sec = 3600;
    if (!service->UpdateNtpConfig(context, disabled) ||
        service->SyncNow(context, live_stream::TimeSyncSource::kNtp)) {
        return 12;
    }

    live_stream::NtpConfig invalid_enabled;
    invalid_enabled.enabled = true;
    invalid_enabled.sync_interval_sec = 3600;
    if (service->UpdateNtpConfig(context, invalid_enabled)) {
        return 13;
    }

    live_stream::NtpConfig too_many_servers;
    too_many_servers.enabled = true;
    too_many_servers.sync_interval_sec = 3600;
    too_many_servers.servers = {"a", "b", "c", "d", "e"};
    if (service->UpdateNtpConfig(context, too_many_servers)) {
        return 14;
    }

    const int64_t last_good_sync_time =
        service->GetTimeStatus().last_sync_time_ms;
    platform.set_ok = false;
    if (service->SetSystemTime(context, 5000,
                               live_stream::TimeSyncSource::kManual) ||
        service->GetTimeStatus().last_sync_ok ||
        service->GetTimeStatus().last_sync_time_ms != last_good_sync_time ||
        logger.last_record.result !=
            live_stream::OperationResult::kFailed) {
        return 15;
    }

    service->Stop();
    if (service->SetTimezone(context, "Asia/Tokyo") ||
        !service->Start() ||
        service->GetTimeStatus().timezone != "UTC") {
        return 16;
    }

    service->Stop();
    return 0;
}
