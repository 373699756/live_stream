#include "alarm_service.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "config_service.h"
#include "event_service.h"
#include "infra/time.h"
#include "json_utils.h"
#include "logger_service.h"

namespace live_stream {
namespace {

constexpr std::size_t kMaxRules = 16;
constexpr std::size_t kMaxAlarmMessageLength = 128;
constexpr uint32_t kMaxAlarmDurationMs = 60U * 60U * 1000U;

bool IsRuleValid(const AlarmRule &rule) {
    return rule.min_duration_ms <= kMaxAlarmDurationMs;
}

bool VerifyActionsConfig(const ConfigJson &value) {
    if (!value.is_object()) {
        return false;
    }
    bool ignored = false;
    bool record = false;
    return json_utils::ReadField(value, "snapshot", &ignored) &&
           json_utils::ReadField(value, "record", &record) &&
           json_utils::ReadField(value, "notify", &ignored) && !record;
}

bool VerifyScheduleConfig(const ConfigJson &value) {
    if (!value.is_object()) {
        return false;
    }
    std::string mode;
    if (!json_utils::ReadField(value, "mode", &mode) || mode != "always" ||
        !value.contains("weekly") || !value.at("weekly").is_array()) {
        return false;
    }
    return true;
}

bool ParseAlarmRuleConfig(const ConfigJson &value, const std::string &name,
                          AlarmSource source, const AlarmRule &fallback,
                          bool required,
                          AlarmRule *rule) {
    if (rule == nullptr || !value.is_object()) {
        return false;
    }
    if (!value.contains(name)) {
        if (required) {
            return false;
        }
        *rule = fallback;
        rule->source = source;
        return true;
    }
    if (!value.at(name).is_object()) {
        return false;
    }
    AlarmRule parsed = fallback;
    parsed.source = source;
    const ConfigJson &rule_config = value.at(name);
    uint32_t sensitivity = 0;
    if (!json_utils::ReadField(rule_config, "enabled", &parsed.enabled) ||
        !json_utils::ReadField(rule_config, "sensitivity", &sensitivity, 0,
                               100) ||
        !json_utils::ReadField(rule_config, "min_duration_ms",
                               &parsed.min_duration_ms, 0,
                               kMaxAlarmDurationMs) ||
        !rule_config.contains("regions") ||
        !rule_config.at("regions").is_array()) {
        return false;
    }
    if (!IsRuleValid(parsed)) {
        return false;
    }
    *rule = parsed;
    return true;
}

bool ParseAlarmConfig(const ConfigJson &value, const AlarmRule &motion_fallback,
                      const AlarmRule &ai_fallback, AlarmRule *motion_rule,
                      AlarmRule *ai_rule) {
    if (motion_rule == nullptr || ai_rule == nullptr || !value.is_object()) {
        return false;
    }
    AlarmRule parsed_motion;
    AlarmRule parsed_ai;
    if (!ParseAlarmRuleConfig(value, "motion_detection", AlarmSource::kMotion,
                              motion_fallback, true, &parsed_motion) ||
        !ParseAlarmRuleConfig(value, "ai_detection",
                              AlarmSource::kAiDetection, ai_fallback,
                              false, &parsed_ai)) {
        return false;
    }
    if (!value.contains("actions") || !VerifyActionsConfig(value.at("actions")) ||
        !value.contains("schedule") ||
        !VerifyScheduleConfig(value.at("schedule"))) {
        return false;
    }
    *motion_rule = parsed_motion;
    *ai_rule = parsed_ai;
    return true;
}

class AlarmServiceImpl : public IAlarmService {
public:
    explicit AlarmServiceImpl(const AlarmServiceOptions &options)
        : options_(options) {
        for (const AlarmRule &rule : options.default_rules) {
            rules_[rule.source] = rule;
        }
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        if (rules_.size() > kMaxRules) {
            return false;
        }
        for (const auto &entry : rules_) {
            if (!IsRuleValid(entry.second)) {
                return false;
            }
        }
        if (options_.config_service != nullptr && !config_attached_) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex_);
                return VerifyConfigLocked(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid alarm config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex_);
                return ApplyConfigLocked(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "apply alarm config failed");
            };
            if (!options_.config_service->AttachConfig("alarm", attachment)) {
                return false;
            }
            config_attached_ = true;
        }
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (options_.config_service != nullptr) {
            const ConfigJson alarm_config =
                options_.config_service->GetValue("alarm");
            if (!alarm_config.is_null()) {
                if (!ApplyConfigLocked(alarm_config)) {
                    return false;
                }
            }
        }
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
        std::lock_guard<std::mutex> lock(mutex_);
        pending_since_ms_.clear();
        status_ = AlarmStatus();
        started_ = false;
        initialized_ = false;
    }

    AlarmStatus GetAlarmStatus() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return AlarmStatus();
        }
        return status_;
    }

    bool UpdateRules(const live_stream::RequestContext &context,
                     const std::vector<AlarmRule> &rules) override {
        if (rules.size() > kMaxRules) {
            RecordAudit(context, OperationResult::kRejected, "alarm",
                        "too_many_rules");
            return false;
        }
        std::map<AlarmSource, AlarmRule> next_rules;
        for (const AlarmRule &rule : rules) {
            if (!IsRuleValid(rule)) {
                RecordAudit(context, OperationResult::kRejected,
                            AlarmSourceToString(rule.source), "invalid_rule");
                return false;
            }
            next_rules[rule.source] = rule;
        }
        if (!IsStarted()) {
            RecordAudit(context, OperationResult::kFailed, "alarm",
                        "service_not_started");
            return false;
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
        return true;
    }

    bool EnableRule(const live_stream::RequestContext &context,
                    AlarmSource source, bool enabled) override {
        if (!IsStarted()) {
            RecordAudit(context, OperationResult::kFailed,
                        AlarmSourceToString(source), "service_not_started");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            AlarmRule &rule = rules_[source];
            rule.source = source;
            rule.enabled = enabled;
            if (!enabled) {
                pending_since_ms_.erase(source);
                if (status_.active && status_.source == source) {
                    status_ = AlarmStatus();
                }
            }
        }
        RecordAudit(context, OperationResult::kSuccess, AlarmSourceToString(source),
                    "");
        return true;
    }

    bool InjectAlarmInput(const AlarmInput &input) override {
        if (input.message.size() > kMaxAlarmMessageLength) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }

        AlarmStatus triggered;
        bool should_publish = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto rule_iter = rules_.find(input.source);
            if (rule_iter == rules_.end() || !rule_iter->second.enabled) {
                return true;
            }

            if (!input.active) {
                pending_since_ms_.erase(input.source);
                if (status_.active && status_.source == input.source) {
                    status_ = AlarmStatus();
                }
                return true;
            }

            const int64_t now = infra::Time::MonotonicMillis();
            int64_t &since = pending_since_ms_[input.source];
            if (since == 0) {
                since = now;
            }
            if (now - since <
                static_cast<int64_t>(rule_iter->second.min_duration_ms)) {
                return true;
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
        return true;
    }

    bool ClearAlarm(const live_stream::RequestContext &context) override {
        if (!IsStarted()) {
            RecordAudit(context, OperationResult::kFailed, "alarm",
                        "service_not_started");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_since_ms_.clear();
            status_ = AlarmStatus();
        }
        RecordAudit(context, OperationResult::kSuccess, "alarm", "");
        return true;
    }

private:
    AlarmRule CurrentRuleLocked(AlarmSource source) const {
        const auto iter = rules_.find(source);
        if (iter == rules_.end()) {
            AlarmRule rule;
            rule.source = source;
            return rule;
        }
        return iter->second;
    }

    bool IsRuleEnabledLocked(AlarmSource source) const {
        const auto iter = rules_.find(source);
        return iter != rules_.end() && iter->second.enabled;
    }

    bool VerifyConfigLocked(const ConfigJson &value) const {
        AlarmRule motion_rule;
        AlarmRule ai_rule;
        return ParseAlarmConfig(value, CurrentRuleLocked(AlarmSource::kMotion),
                                CurrentRuleLocked(AlarmSource::kAiDetection),
                                &motion_rule, &ai_rule);
    }

    bool ApplyConfigLocked(const ConfigJson &value) {
        AlarmRule motion_rule;
        AlarmRule ai_rule;
        if (!ParseAlarmConfig(value, CurrentRuleLocked(AlarmSource::kMotion),
                              CurrentRuleLocked(AlarmSource::kAiDetection),
                              &motion_rule, &ai_rule)) {
            return false;
        }
        rules_[AlarmSource::kMotion] = motion_rule;
        rules_[AlarmSource::kAiDetection] = ai_rule;
        if (!motion_rule.enabled) {
            ClearSourceLocked(AlarmSource::kMotion);
        }
        if (!ai_rule.enabled) {
            ClearSourceLocked(AlarmSource::kAiDetection);
        }
        return true;
    }

    void ClearSourceLocked(AlarmSource source) {
        pending_since_ms_.erase(source);
        if (status_.active && status_.source == source) {
            status_ = AlarmStatus();
        }
    }

    void PublishAlarmTriggered(const AlarmStatus &status) {
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

    void RecordAudit(const live_stream::RequestContext &context,
                     OperationResult result, const std::string &target,
                     const std::string &reason) {
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
    mutable std::mutex mutex_;
    bool config_attached_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IAlarmService>
CreateAlarmService(const AlarmServiceOptions &options) {
    return std::unique_ptr<IAlarmService>(new AlarmServiceImpl(options));
}

const char *AlarmSourceToString(AlarmSource source) {
    switch (source) {
        case AlarmSource::kMotion:
            return "motion";
        case AlarmSource::kAiDetection:
            return "ai_detection";
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
