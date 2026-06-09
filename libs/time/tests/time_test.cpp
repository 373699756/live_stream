#include "time_api.h"

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

class FakeEvent : public live_stream::IEvent {
public:
  bool Start() override { return true; }
  void Stop() override {}

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

  live_stream::TimeOptions lifecycle_options;
  lifecycle_options.default_timezone = "UTC";
  lifecycle_options.default_ntp_config.enabled = false;
  std::unique_ptr<live_stream::ITime> lifecycle_time =
      live_stream::CreateTime(lifecycle_options);
  if (!lifecycle_time || lifecycle_time->IsStarted()) {
    return 2;
  }
  if (!lifecycle_time->Start() || !lifecycle_time->IsStarted()) {
    return 3;
  }
  lifecycle_time->Stop();
  if (lifecycle_time->SetSystemTime(live_stream::RequestContext(), 4000,
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
      event.last_event.type != live_stream::EventType::kTimeChanged ||
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
