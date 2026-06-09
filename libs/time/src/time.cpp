#include "time_api.h"

#include "config.h"
#include "event.h"
#include "infra/time.h"
#include "json_utils.h"
#include "logger.h"

#include <mutex>
#include <utility>

namespace live_stream {
namespace {

constexpr const char *kConfigName = "time";
constexpr std::size_t kMaxTimezoneLength = 64;
constexpr std::size_t kMaxNtpServers = 4;
constexpr std::size_t kMaxNtpServerLength = 128;

bool IsNtpConfigValid(const NtpConfig &config) {
    if (config.enabled &&
        (config.servers.empty() || config.servers.size() > kMaxNtpServers ||
         config.sync_interval_sec == 0)) {
        return false;
    }
    for (const std::string &server : config.servers) {
        if (server.empty() || server.size() > kMaxNtpServerLength) {
            return false;
        }
    }
    return true;
}

OperationResult ToOperationResult(bool ok) {
    return ok ? OperationResult::kSuccess : OperationResult::kFailed;
}

void NormalizeTimeConfig(TimeConfig *config) {
    if (config == nullptr) {
        return;
    }
    if (!config->manual_sync_allowed) {
        config->browser_sync_on_login = false;
    }
}

bool IsTimeConfigValid(const TimeConfig &config) {
    return !config.timezone.empty() &&
           config.timezone.size() <= kMaxTimezoneLength &&
           IsNtpConfigValid(config.ntp);
}

bool LoadTimeConfig(const ConfigJson &value, TimeConfig *config) {
    if (config == nullptr || !value.is_object()) {
        return false;
    }
    TimeConfig parsed;
    if (!json_utils::ReadField(value, "timezone", &parsed.timezone) ||
        !value.contains("ntp") || !value.at("ntp").is_object()) {
        return false;
    }
    if (value.contains("manual_sync_allowed") &&
        !json_utils::ReadField(value, "manual_sync_allowed",
                               &parsed.manual_sync_allowed)) {
        return false;
    }
    if (value.contains("browser_sync_on_login") &&
        !json_utils::ReadField(value, "browser_sync_on_login",
                               &parsed.browser_sync_on_login)) {
        return false;
    }
    const ConfigJson &ntp = value.at("ntp");
    if (!json_utils::ReadField(ntp, "enabled", &parsed.ntp.enabled) ||
        !json_utils::ReadStringArray(ntp, "servers", &parsed.ntp.servers) ||
        !json_utils::ReadField(ntp, "sync_interval_sec",
                          &parsed.ntp.sync_interval_sec, 1, 0xffffffffU)) {
        return false;
    }
    NormalizeTimeConfig(&parsed);
    if (!IsTimeConfigValid(parsed)) {
        return false;
    }
    *config = parsed;
    return true;
}

ConfigJson BuildTimeConfig(const ConfigJson &current,
                           const TimeConfig &config) {
    ConfigJson root = current.is_object() ? current : ConfigJson::object();
    root["timezone"] = config.timezone;
    root["manual_sync_allowed"] = config.manual_sync_allowed;
    root["browser_sync_on_login"] = config.browser_sync_on_login;
    ConfigJson ntp = ConfigJson::object();
    ntp["enabled"] = config.ntp.enabled;
    ConfigJson servers = ConfigJson::array();
    for (const std::string &server : config.ntp.servers) {
        servers.push_back(server);
    }
    ntp["servers"] = servers;
    ntp["sync_interval_sec"] = config.ntp.sync_interval_sec;
    root["ntp"] = ntp;
    return root;
}

class TimeImpl : public ITime {
public:
    explicit TimeImpl(const TimeOptions &options)
        : options_(options) {
        status_.timezone = options.default_timezone;
        status_.ntp = options.default_ntp_config;
    }

    bool Prepare() {
        TimeConfig loaded_config;
        loaded_config.timezone = status_.timezone;
        loaded_config.ntp = status_.ntp;
        loaded_config.manual_sync_allowed = manual_sync_allowed_;
        loaded_config.browser_sync_on_login = browser_sync_on_login_;
        ConfigJson loaded_json = ConfigJson::object();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) {
                return true;
            }
            if (!IsTimezoneValid(status_.timezone) ||
                !IsNtpConfigValid(status_.ntp)) {
                return false;
            }
        }

        if (options_.config != nullptr) {
            ConfigJson value = options_.config->GetValue(kConfigName);
            if (value.is_object()) {
                if (!LoadTimeConfig(value, &loaded_config)) {
                    return false;
                }
                loaded_json = value;
            } else {
                loaded_json = BuildTimeConfig(loaded_json, loaded_config);
            }
        } else {
            loaded_json = BuildTimeConfig(loaded_json, loaded_config);
        }

        const int64_t now_ms = ReadSystemTimeMs();

        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        status_.timezone = loaded_config.timezone;
        status_.ntp = loaded_config.ntp;
        status_.manual_sync_allowed = loaded_config.manual_sync_allowed;
        status_.browser_sync_on_login = loaded_config.browser_sync_on_login;
        manual_sync_allowed_ = loaded_config.manual_sync_allowed;
        browser_sync_on_login_ = loaded_config.browser_sync_on_login;
        time_config_json_ = loaded_json;
        if (options_.config != nullptr && !config_attached_) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                return VerifyTimeConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid time config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                return ApplyTimeConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "apply time config failed");
            };
            if (!options_.config->AttachConfig(kConfigName, attachment)) {
                return false;
            }
            config_attached_ = true;
        }
        status_.system_time_ms = now_ms;
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
            time_config_json_ = ConfigJson::object();
            suppress_config_apply_ = false;
            config_attached_ = false;
            started_ = false;
            initialized_ = false;
        }
        if (detach && options_.config != nullptr) {
            static_cast<void>(options_.config->DetachConfig(kConfigName));
        }
    }

    TimeStatus GetTimeStatus() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_) {
                return TimeStatus{};
            }
        }

        const int64_t now_ms = ReadSystemTimeMs();

        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return TimeStatus{};
        }
        TimeStatus status = status_;
        status.system_time_ms = now_ms;
        return status;
    }

    bool SetTimezone(const live_stream::RequestContext &context,
                     const std::string &timezone) override {
        if (!IsTimezoneValid(timezone)) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }
        TimeConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = CurrentConfigLocked();
        }
        config.timezone = timezone;
        return ApplyTimeConfigUpdate(context, config, "timezone");
    }

    bool UpdateTimeConfig(const live_stream::RequestContext &context,
                          const TimeConfig &config) override {
        return ApplyTimeConfigUpdate(context, config, "time_config");
    }

    bool SetSystemTime(const live_stream::RequestContext &context,
                       int64_t unix_time_ms, TimeSyncSource source) override {
        if (unix_time_ms <= 0) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }
        if (source == TimeSyncSource::kManual) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!manual_sync_allowed_) {
                return false;
            }
        }
        if (source == TimeSyncSource::kBrowser) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!manual_sync_allowed_) {
                return false;
            }
        }
        if (options_.platform == nullptr) {
            UpdateSyncState(source, 0, false);
            RecordAudit(context, false, TimeSyncSourceToString(source));
            return false;
        }
        const bool ok = options_.platform->SetSystemTimeMs(unix_time_ms);
        UpdateSyncState(source, unix_time_ms, ok);
        RecordAudit(context, ok, TimeSyncSourceToString(source));
        if (ok) {
            PublishTimeChanged(TimeSyncSourceToString(source));
        }
        return ok;
    }

    bool SyncNow(const live_stream::RequestContext &context,
                 TimeSyncSource source) override {
        if (!IsStarted()) {
            return false;
        }
        if (options_.platform == nullptr) {
            UpdateSyncState(source, 0, false);
            RecordAudit(context, false, TimeSyncSourceToString(source));
            return false;
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
            return false;
        }

        int64_t synced_time_ms = 0;
        bool ok = options_.platform->SyncNtp(config.servers, &synced_time_ms);
        if (ok) {
            ok = options_.platform->SetSystemTimeMs(synced_time_ms);
        }
        UpdateSyncState(source, synced_time_ms, ok);
        RecordAudit(context, ok, "ntp");
        if (ok) {
            PublishTimeChanged("ntp");
        }
        return ok;
    }

    bool UpdateNtpConfig(const live_stream::RequestContext &context,
                         const NtpConfig &config) override {
        if (!IsStarted()) {
            return false;
        }
        TimeConfig time_config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            time_config = CurrentConfigLocked();
        }
        time_config.ntp = config;
        return ApplyTimeConfigUpdate(context, time_config, "ntp_config");
    }

    bool UpdateBrowserSyncConfig(const live_stream::RequestContext &context,
                                 bool manual_sync_allowed,
                                 bool browser_sync_on_login) override {
        if (!IsStarted()) {
            return false;
        }
        TimeConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = CurrentConfigLocked();
        }
        config.manual_sync_allowed = manual_sync_allowed;
        config.browser_sync_on_login = browser_sync_on_login;
        return ApplyTimeConfigUpdate(context, config, "browser_sync_config");
    }

private:
    bool IsTimezoneValid(const std::string &timezone) const {
        return !timezone.empty() && timezone.size() <= kMaxTimezoneLength;
    }

    int64_t ReadSystemTimeMs() const {
        if (options_.platform != nullptr) {
            return options_.platform->GetSystemTimeMs();
        }
        return infra::Time::SystemTimeMillis();
    }

    bool VerifyTimeConfig(const ConfigJson &value) const {
        TimeConfig config;
        return LoadTimeConfig(value, &config);
    }

    bool ApplyTimeConfig(const ConfigJson &value) {
        TimeConfig config;
        if (!LoadTimeConfig(value, &config)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (suppress_config_apply_) {
                return true;
            }
            ApplyLoadedConfigLocked(config);
            time_config_json_ = value;
        }
        PublishTimeChanged("config");
        return true;
    }

    bool ApplyTimeConfigUpdate(const live_stream::RequestContext &context,
                               const TimeConfig &config,
                               const std::string &target) {
        if (!IsStarted()) {
            return false;
        }
        TimeConfig normalized_config = config;
        NormalizeTimeConfig(&normalized_config);
        if (!IsTimeConfigValid(normalized_config)) {
            return false;
        }
        ConfigJson current_json = ConfigJson::object();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_json = time_config_json_;
        }
        if (!PersistConfig(current_json, normalized_config)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ApplyLoadedConfigLocked(normalized_config);
            time_config_json_ = BuildTimeConfig(time_config_json_,
                                                normalized_config);
        }
        PublishTimeChanged(target);
        RecordAudit(context, true, target);
        return true;
    }

    TimeConfig CurrentConfigLocked() const {
        TimeConfig config;
        config.timezone = status_.timezone;
        config.ntp = status_.ntp;
        config.manual_sync_allowed = manual_sync_allowed_;
        config.browser_sync_on_login = browser_sync_on_login_;
        return config;
    }

    void ApplyLoadedConfigLocked(const TimeConfig &config) {
        status_.timezone = config.timezone;
        status_.ntp = config.ntp;
        status_.manual_sync_allowed = config.manual_sync_allowed;
        status_.browser_sync_on_login = config.browser_sync_on_login;
        manual_sync_allowed_ = config.manual_sync_allowed;
        browser_sync_on_login_ = config.browser_sync_on_login;
    }

    bool PersistConfig(const ConfigJson &current_json,
                       const TimeConfig &config) {
        if (options_.config == nullptr) {
            return true;
        }
        const ConfigJson next_json = BuildTimeConfig(current_json, config);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            suppress_config_apply_ = true;
        }
        const bool ok = options_.config->SetValue(kConfigName, next_json);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            suppress_config_apply_ = false;
            if (ok) {
                time_config_json_ = next_json;
            }
        }
        return ok;
    }

    void UpdateSyncState(TimeSyncSource source, int64_t sync_time_ms, bool ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.last_sync_source = source;
        status_.last_sync_time_ms = ok ? sync_time_ms : status_.last_sync_time_ms;
        status_.last_sync_ok = ok;
        if (ok) {
            status_.system_time_ms = sync_time_ms;
        }
    }

    void PublishTimeChanged(const std::string &message) {
        if (options_.event == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kTimeChanged;
        event.source = "time";
        event.message = message;
        static_cast<void>(options_.event->Publish(event));
    }

    void RecordAudit(const live_stream::RequestContext &context, bool ok,
                     const std::string &target) {
        if (options_.logger == nullptr) {
            return;
        }
        OperationRecord record;
        record.timestamp_ms = infra::Time::SystemTimeMillis();
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = "time";
        record.action = OperationAction::kTimeSync;
        record.target = target;
        record.result = ToOperationResult(ok);
        record.reason = ok ? "ok" : "failed";
        static_cast<void>(options_.logger->RecordOperation(record));
    }

    TimeOptions options_;
    TimeStatus status_;
    ConfigJson time_config_json_ = ConfigJson::object();
    mutable std::mutex mutex_;
    bool manual_sync_allowed_ = true;
    bool browser_sync_on_login_ = true;
    bool config_attached_ = false;
    bool suppress_config_apply_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ITime>
CreateTime(const TimeOptions &options) {
    return std::unique_ptr<ITime>(new TimeImpl(options));
}

const char *TimeSyncSourceToString(TimeSyncSource source) {
    switch (source) {
        case TimeSyncSource::kManual:
            return "manual";
        case TimeSyncSource::kOnvif:
            return "onvif";
        case TimeSyncSource::kNtp:
            return "ntp";
        case TimeSyncSource::kBrowser:
            return "browser";
    }
    return "unknown";
}

}  // namespace live_stream
