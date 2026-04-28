#include "alarm_service.h"

#include "config_service.h"
#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace live_stream {
namespace {

constexpr std::size_t kMaxRules = 16;
constexpr std::size_t kMaxAlarmMessageLength = 128;
constexpr uint32_t kMaxAlarmDurationMs = 60U * 60U * 1000U;

bool IsRuleValid(const AlarmRule& rule) {
    return rule.min_duration_ms <= kMaxAlarmDurationMs;
}

bool ReadOptionalBool(const ConfigJson& object,
                      const char* key,
                      bool* value) {
    if (value == nullptr) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    const ConfigJson& field = object[key];
    if (!field.is_boolean()) {
        return false;
    }
    *value = field.get<bool>();
    return true;
}

bool ReadOptionalUint32(const ConfigJson& object,
                        const char* key,
                        uint32_t max_value,
                        uint32_t* value) {
    if (value == nullptr) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    const ConfigJson& field = object[key];
    uint64_t parsed = 0;
    if (field.is_number_unsigned()) {
        parsed = field.get<uint64_t>();
    } else if (field.is_number_integer()) {
        const int64_t signed_value = field.get<int64_t>();
        if (signed_value < 0) {
            return false;
        }
        parsed = static_cast<uint64_t>(signed_value);
    } else {
        return false;
    }
    if (parsed > max_value) {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool VerifyActionsConfig(const ConfigJson& value) {
    if (!value.is_object()) {
        return false;
    }
    bool ignored = false;
    return ReadOptionalBool(value, "snapshot", &ignored) &&
           ReadOptionalBool(value, "record", &ignored) &&
           ReadOptionalBool(value, "notify", &ignored);
}

bool VerifyScheduleConfig(const ConfigJson& value) {
    if (!value.is_object()) {
        return false;
    }
    if (value.contains("mode")) {
        const ConfigJson& mode = value["mode"];
        if (!mode.is_string() || mode.get<std::string>() != "always") {
            return false;
        }
    }
    if (value.contains("weekly") && !value["weekly"].is_array()) {
        return false;
    }
    return true;
}

infra::Result<AlarmRule> ParseAlarmConfig(const ConfigJson& value,
                                          const AlarmRule& fallback) {
    if (!value.is_object()) {
        return infra::Result<AlarmRule>::Fail(infra::Status::kInvalidParam);
    }

    AlarmRule motion_rule = fallback;
    motion_rule.source = AlarmSource::kMotion;
    if (value.contains("motion_detection")) {
        const ConfigJson& motion = value["motion_detection"];
        if (!motion.is_object()) {
            return infra::Result<AlarmRule>::Fail(
                infra::Status::kInvalidParam);
        }
        uint32_t sensitivity = 50;
        if (!ReadOptionalBool(motion, "enabled", &motion_rule.enabled) ||
            !ReadOptionalUint32(motion, "sensitivity", 100, &sensitivity) ||
            !ReadOptionalUint32(motion, "min_duration_ms",
                                kMaxAlarmDurationMs,
                                &motion_rule.min_duration_ms)) {
            return infra::Result<AlarmRule>::Fail(
                infra::Status::kInvalidParam);
        }
        if (motion.contains("regions") && !motion["regions"].is_array()) {
            return infra::Result<AlarmRule>::Fail(
                infra::Status::kInvalidParam);
        }
    }
    if (value.contains("actions") && !VerifyActionsConfig(value["actions"])) {
        return infra::Result<AlarmRule>::Fail(infra::Status::kInvalidParam);
    }
    if (value.contains("schedule") &&
        !VerifyScheduleConfig(value["schedule"])) {
        return infra::Result<AlarmRule>::Fail(infra::Status::kInvalidParam);
    }
    if (!IsRuleValid(motion_rule)) {
        return infra::Result<AlarmRule>::Fail(infra::Status::kInvalidParam);
    }
    return infra::Result<AlarmRule>::Ok(motion_rule);
}

class AlarmServiceImpl : public IAlarmService {
 public:
    explicit AlarmServiceImpl(const AlarmServiceOptions& options)
        : options_(options) {
        for (const AlarmRule& rule : options.default_rules) {
            rules_[rule.source] = rule;
        }
    }

    infra::Status Init() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (rules_.size() > kMaxRules) {
            return infra::Status::kInvalidParam;
        }
        for (const auto& entry : rules_) {
            if (!IsRuleValid(entry.second)) {
                return infra::Status::kInvalidParam;
            }
        }
        if (options_.config_service != nullptr && !config_callbacks_registered_) {
            infra::Status status = options_.config_service->RegisterVerify(
                "alarm", [this](const ConfigJson& value) {
                    std::lock_guard<std::mutex> guard(mutex_);
                    return VerifyConfigLocked(value);
                });
            if (status != infra::Status::kOk) {
                return status;
            }
            status = options_.config_service->RegisterApply(
                "alarm", [this](const ConfigJson& value) {
                    std::lock_guard<std::mutex> guard(mutex_);
                    return ApplyConfigLocked(value);
                });
            if (status != infra::Status::kOk) {
                return status;
            }
            config_callbacks_registered_ = true;
        }
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return infra::Status::kBusy;
        }
        if (options_.config_service != nullptr) {
            ConfigJson alarm_config;
            if (options_.config_service->GetValue("alarm", &alarm_config) ==
                infra::Status::kOk) {
                const infra::Status status = ApplyConfigLocked(alarm_config);
                if (status != infra::Status::kOk) {
                    return status;
                }
            }
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
        pending_since_ms_.clear();
        status_ = AlarmStatus();
        started_ = false;
        initialized_ = false;
    }

    const char* Name() const override { return "alarm_service"; }

    infra::Result<AlarmStatus> GetAlarmStatus() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return infra::Result<AlarmStatus>::Fail(infra::Status::kBusy);
        }
        return infra::Result<AlarmStatus>::Ok(status_);
    }

    infra::Status UpdateRules(const infra::RequestContext& context,
                             const std::vector<AlarmRule>& rules) override {
        if (rules.size() > kMaxRules) {
            RecordAudit(context, OperationResult::kRejected, "alarm",
                        "too_many_rules");
            return infra::Status::kInvalidParam;
        }
        std::map<AlarmSource, AlarmRule> next_rules;
        for (const AlarmRule& rule : rules) {
            if (!IsRuleValid(rule)) {
                RecordAudit(context, OperationResult::kRejected,
                            AlarmSourceToString(rule.source),
                            "invalid_rule");
                return infra::Status::kInvalidParam;
            }
            next_rules[rule.source] = rule;
        }
        if (!IsStarted()) {
            RecordAudit(context, OperationResult::kFailed, "alarm",
                        "service_not_started");
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            rules_.swap(next_rules);
            pending_since_ms_.clear();
            if (status_.active && !IsRuleEnabledLocked(status_.source)) {
                status_ = AlarmStatus();
            }
        }
        RecordAudit(context, OperationResult::kSuccess, "alarm", "");
        return infra::Status::kOk;
    }

    infra::Status EnableRule(const infra::RequestContext& context,
                            AlarmSource source,
                            bool enabled) override {
        if (!IsStarted()) {
            RecordAudit(context, OperationResult::kFailed,
                        AlarmSourceToString(source), "service_not_started");
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            AlarmRule& rule = rules_[source];
            rule.source = source;
            rule.enabled = enabled;
            if (!enabled) {
                pending_since_ms_.erase(source);
                if (status_.active && status_.source == source) {
                    status_ = AlarmStatus();
                }
            }
        }
        RecordAudit(context, OperationResult::kSuccess,
                    AlarmSourceToString(source), "");
        return infra::Status::kOk;
    }

    infra::Status InjectAlarmInput(const AlarmInput& input) override {
        if (input.message.size() > kMaxAlarmMessageLength) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }

        AlarmStatus triggered;
        bool should_publish = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto rule_iter = rules_.find(input.source);
            if (rule_iter == rules_.end() || !rule_iter->second.enabled) {
                return infra::Status::kOk;
            }

            if (!input.active) {
                pending_since_ms_.erase(input.source);
                if (status_.active && status_.source == input.source) {
                    status_ = AlarmStatus();
                }
                return infra::Status::kOk;
            }

            const int64_t now = infra::Time::MonotonicMillis();
            int64_t& since = pending_since_ms_[input.source];
            if (since == 0) {
                since = now;
            }
            if (now - since < static_cast<int64_t>(
                                  rule_iter->second.min_duration_ms)) {
                return infra::Status::kOk;
            }
            if (!status_.active || status_.source != input.source) {
                status_.active = true;
                status_.source = input.source;
                status_.active_since_ms = since;
                status_.last_trigger_time_ms = now;
                status_.message = input.message;
                triggered = status_;
                should_publish = true;
            }
        }

        if (should_publish) {
            PublishAlarmTriggered(triggered);
        }
        return infra::Status::kOk;
    }

    infra::Status ClearAlarm(const infra::RequestContext& context) override {
        if (!IsStarted()) {
            RecordAudit(context, OperationResult::kFailed, "alarm",
                        "service_not_started");
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_since_ms_.clear();
            status_ = AlarmStatus();
        }
        RecordAudit(context, OperationResult::kSuccess, "alarm", "");
        return infra::Status::kOk;
    }

 private:
    bool IsStarted() {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    AlarmRule CurrentMotionRuleLocked() const {
        const auto iter = rules_.find(AlarmSource::kMotion);
        if (iter == rules_.end()) {
            AlarmRule rule;
            rule.source = AlarmSource::kMotion;
            return rule;
        }
        return iter->second;
    }

    bool IsRuleEnabledLocked(AlarmSource source) const {
        const auto iter = rules_.find(source);
        return iter != rules_.end() && iter->second.enabled;
    }

    infra::Status VerifyConfigLocked(const ConfigJson& value) const {
        return ParseAlarmConfig(value, CurrentMotionRuleLocked()).status;
    }

    infra::Status ApplyConfigLocked(const ConfigJson& value) {
        infra::Result<AlarmRule> parsed =
            ParseAlarmConfig(value, CurrentMotionRuleLocked());
        if (!parsed.IsOk()) {
            return parsed.status;
        }
        rules_[AlarmSource::kMotion] = parsed.value;
        if (!parsed.value.enabled) {
            pending_since_ms_.erase(AlarmSource::kMotion);
            if (status_.active && status_.source == AlarmSource::kMotion) {
                status_ = AlarmStatus();
            }
        }
        return infra::Status::kOk;
    }

    void PublishAlarmTriggered(const AlarmStatus& status) {
        if (options_.event_service == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kAlarmTriggered;
        event.source = "alarm_service";
        event.target = AlarmSourceToString(status.source);
        event.message = status.message;
        event.value = 1;
        static_cast<void>(options_.event_service->Publish(event));
    }

    void RecordAudit(const infra::RequestContext& context,
                     OperationResult result,
                     const std::string& target,
                     const std::string& reason) {
        if (options_.logger_service == nullptr) {
            return;
        }
        OperationRecord record;
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = "alarm_service";
        record.action = OperationAction::kModifyConfig;
        record.target = target;
        record.result = result;
        record.reason = reason;
        static_cast<void>(options_.logger_service->RecordOperation(record));
    }

    AlarmServiceOptions options_;
    std::map<AlarmSource, AlarmRule> rules_;
    std::map<AlarmSource, int64_t> pending_since_ms_;
    AlarmStatus status_;
    std::mutex mutex_;
    bool config_callbacks_registered_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IAlarmService> CreateAlarmService(
    const AlarmServiceOptions& options) {
    return std::unique_ptr<IAlarmService>(new AlarmServiceImpl(options));
}

const char* AlarmSourceToString(AlarmSource source) {
    switch (source) {
        case AlarmSource::kMotion:
            return "motion";
        case AlarmSource::kIoInput:
            return "io_input";
        case AlarmSource::kTamper:
            return "tamper";
        case AlarmSource::kNetwork:
            return "network";
    }
    return "unknown";
}

}  // namespace live_stream
