#include "time_service.h"

#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include <mutex>
#include <utility>

namespace live_stream {
namespace {

constexpr std::size_t kMaxTimezoneLength = 64;
constexpr std::size_t kMaxNtpServers = 4;
constexpr std::size_t kMaxNtpServerLength = 128;

bool IsNtpConfigValid(const NtpConfig& config) {
    if (config.enabled &&
        (config.servers.empty() || config.servers.size() > kMaxNtpServers ||
         config.sync_interval_sec == 0)) {
        return false;
    }
    for (const std::string& server : config.servers) {
        if (server.empty() || server.size() > kMaxNtpServerLength) {
            return false;
        }
    }
    return true;
}

OperationResult ToOperationResult(infra::Status error) {
    return error == infra::Status::kOk ? OperationResult::kSuccess
                                      : OperationResult::kFailed;
}

class TimeServiceImpl : public ITimeService {
 public:
    explicit TimeServiceImpl(const TimeServiceOptions& options)
        : options_(options) {
        status_.timezone = options.default_timezone;
        status_.ntp = options.default_ntp_config;
    }

    infra::Status Init() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) {
                return infra::Status::kOk;
            }
            if (!IsTimezoneValid(status_.timezone) ||
                !IsNtpConfigValid(status_.ntp)) {
                return infra::Status::kInvalidParam;
            }
        }

        const int64_t now_ms = ReadSystemTimeMs();

        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        status_.system_time_ms = now_ms;
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
        started_ = false;
        initialized_ = false;
    }

    const char* Name() const override { return "time_service"; }

    infra::Result<TimeStatus> GetTimeStatus() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_) {
                return infra::Result<TimeStatus>::Fail(infra::Status::kBusy);
            }
        }

        const int64_t now_ms = ReadSystemTimeMs();

        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return infra::Result<TimeStatus>::Fail(infra::Status::kBusy);
        }
        TimeStatus status = status_;
        status.system_time_ms = now_ms;
        return infra::Result<TimeStatus>::Ok(status);
    }

    infra::Status SetTimezone(const infra::RequestContext& context,
                             const std::string& timezone) override {
        if (!IsTimezoneValid(timezone)) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.timezone = timezone;
        }
        PublishTimeChanged("timezone");
        RecordAudit(context, infra::Status::kOk, "timezone");
        return infra::Status::kOk;
    }

    infra::Status SetSystemTime(const infra::RequestContext& context,
                               int64_t unix_time_ms,
                               TimeSyncSource source) override {
        if (unix_time_ms <= 0) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        if (options_.platform == nullptr) {
            UpdateSyncState(source, 0, infra::Status::kNotSupported);
            RecordAudit(context, infra::Status::kNotSupported,
                        TimeSyncSourceToString(source));
            return infra::Status::kNotSupported;
        }
        const infra::Status error = options_.platform->SetSystemTimeMs(unix_time_ms);
        UpdateSyncState(source, unix_time_ms, error);
        RecordAudit(context, error, TimeSyncSourceToString(source));
        if (error == infra::Status::kOk) {
            PublishTimeChanged(TimeSyncSourceToString(source));
        }
        return error;
    }

    infra::Status SyncNow(const infra::RequestContext& context,
                         TimeSyncSource source) override {
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        if (options_.platform == nullptr) {
            UpdateSyncState(source, 0, infra::Status::kNotSupported);
            RecordAudit(context, infra::Status::kNotSupported,
                        TimeSyncSourceToString(source));
            return infra::Status::kNotSupported;
        }

        if (source != TimeSyncSource::kNtp) {
            return SetSystemTime(context, options_.platform->GetSystemTimeMs(),
                                 source);
        }

        NtpConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = status_.ntp;
        }
        if (!config.enabled || !IsNtpConfigValid(config)) {
            return infra::Status::kInvalidParam;
        }

        int64_t synced_time_ms = 0;
        infra::Status error = options_.platform->SyncNtp(config.servers,
                                                        &synced_time_ms);
        if (error == infra::Status::kOk) {
            error = options_.platform->SetSystemTimeMs(synced_time_ms);
        }
        UpdateSyncState(source, synced_time_ms, error);
        RecordAudit(context, error, "ntp");
        if (error == infra::Status::kOk) {
            PublishTimeChanged("ntp");
        }
        return error;
    }

    infra::Status UpdateNtpConfig(const infra::RequestContext& context,
                                 const NtpConfig& config) override {
        if (!IsNtpConfigValid(config)) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.ntp = config;
        }
        RecordAudit(context, infra::Status::kOk, "ntp_config");
        return infra::Status::kOk;
    }

 private:
    bool IsTimezoneValid(const std::string& timezone) const {
        return !timezone.empty() && timezone.size() <= kMaxTimezoneLength;
    }

    int64_t ReadSystemTimeMs() const {
        if (options_.platform != nullptr) {
            return options_.platform->GetSystemTimeMs();
        }
        return infra::Time::SystemTimeMillis();
    }

    bool IsStarted() {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void UpdateSyncState(TimeSyncSource source,
                         int64_t sync_time_ms,
                         infra::Status error) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.last_sync_source = source;
        status_.last_sync_time_ms =
            error == infra::Status::kOk ? sync_time_ms : status_.last_sync_time_ms;
        status_.last_sync_error = error;
        if (error == infra::Status::kOk) {
            status_.system_time_ms = sync_time_ms;
        }
    }

    void PublishTimeChanged(const std::string& message) {
        if (options_.event_service == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kTimeChanged;
        event.source = "time_service";
        event.message = message;
        static_cast<void>(options_.event_service->Publish(event));
    }

    void RecordAudit(const infra::RequestContext& context,
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
        record.module = "time_service";
        record.action = OperationAction::kTimeSync;
        record.target = target;
        record.result = ToOperationResult(error);
        record.reason = infra::StatusToString(error);
        static_cast<void>(options_.logger_service->RecordOperation(record));
    }

    TimeServiceOptions options_;
    TimeStatus status_;
    std::mutex mutex_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ITimeService> CreateTimeService(
    const TimeServiceOptions& options) {
    return std::unique_ptr<ITimeService>(new TimeServiceImpl(options));
}

const char* TimeSyncSourceToString(TimeSyncSource source) {
    switch (source) {
        case TimeSyncSource::kManual:
            return "manual";
        case TimeSyncSource::kOnvif:
            return "onvif";
        case TimeSyncSource::kNtp:
            return "ntp";
    }
    return "unknown";
}

}  // namespace live_stream
