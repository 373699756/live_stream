#include "alarm.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "event.h"
#include "infra/time.h"
#include "json_reader.h"
#include "logger.h"

namespace live_stream {
namespace {

constexpr std::size_t kAlarmSourcesTotal = 5;
constexpr std::size_t kMaxAlarmMessageLength = 128;
constexpr uint32_t kMaxAlarmDurationMs = 60U * 60U * 1000U;
constexpr uint8_t kMaxAlarmLevel = 5;

const std::array<AlarmSource, kAlarmSourcesTotal> kAlarmSources = {
    {AlarmSource::kMotion, AlarmSource::kAiDetection, AlarmSource::kIoInput,
     AlarmSource::kTamper, AlarmSource::kNetwork}};

bool AlarmSourceIndex(AlarmSource source, std::size_t *index) {
    if (index == nullptr) {
        return false;
    }
    switch (source) {
        case AlarmSource::kMotion:
            *index = 0;
            return true;
        case AlarmSource::kAiDetection:
            *index = 1;
            return true;
        case AlarmSource::kIoInput:
            *index = 2;
            return true;
        case AlarmSource::kTamper:
            *index = 3;
            return true;
        case AlarmSource::kNetwork:
            *index = 4;
            return true;
    }
    return false;
}

AlarmRule MakeDefaultRule(AlarmSource source) {
    AlarmRule rule;
    rule.source = source;
    return rule;
}

AlarmSourceState MakeDefaultSourceState(AlarmSource source) {
    AlarmSourceState state;
    state.source = source;
    state.level = 1;
    return state;
}

bool IsRuleValid(const AlarmRule &rule) {
    std::size_t index = 0;
    return AlarmSourceIndex(rule.source, &index) &&
           rule.min_duration_ms <= kMaxAlarmDurationMs &&
           rule.repeat_interval_ms <= kMaxAlarmDurationMs &&
           rule.level <= kMaxAlarmLevel;
}

bool ReadOptionalBool(const Json &object, const char *key, bool *value) {
    if (!object.contains(key)) {
        return true;
    }
    return json_reader::ReadField(object, key, value);
}

bool ReadOptionalUint32(const Json &object, const char *key,
                        uint32_t min_value, uint32_t max_value,
                        uint32_t *value) {
    if (!object.contains(key)) {
        return true;
    }
    return json_reader::ReadField(object, key, value, min_value, max_value);
}

bool ReadOptionalLevel(const Json &object, const char *key,
                       uint8_t *value) {
    if (!object.contains(key)) {
        return true;
    }
    uint32_t parsed = 0;
    if (!json_reader::ReadField(object, key, &parsed, 0, kMaxAlarmLevel)) {
        return false;
    }
    *value = static_cast<uint8_t>(parsed);
    return true;
}

bool VerifyActionsConfig(const Json &value) {
    if (!value.is_object()) {
        return false;
    }
    bool ignored = false;
    return json_reader::ReadField(value, "snapshot", &ignored) &&
           json_reader::ReadField(value, "notify", &ignored);
}

bool VerifyScheduleConfig(const Json &value) {
    if (!value.is_object()) {
        return false;
    }
    std::string mode;
    if (!json_reader::ReadField(value, "mode", &mode) || mode != "always" ||
        !value.contains("weekly") || !value.at("weekly").is_array()) {
        return false;
    }
    return true;
}

bool ParseAlarmRuleConfig(const Json &value, const std::string &name,
                          AlarmSource source, const AlarmRule &fallback,
                          bool required, AlarmRule *rule) {
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
    const Json &rule_config = value.at(name);
    uint32_t sensitivity = 0;
    if (!json_reader::ReadField(rule_config, "enabled", &parsed.enabled) ||
        !json_reader::ReadField(rule_config, "sensitivity", &sensitivity, 0,
                               100) ||
        !json_reader::ReadField(rule_config, "min_duration_ms",
                               &parsed.min_duration_ms, 0,
                               kMaxAlarmDurationMs) ||
        !ReadOptionalUint32(rule_config, "repeat_interval_ms", 0,
                            kMaxAlarmDurationMs,
                            &parsed.repeat_interval_ms) ||
        !ReadOptionalBool(rule_config, "manual_clear", &parsed.manual_clear) ||
        !ReadOptionalLevel(rule_config, "level", &parsed.level) ||
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

bool ParseAlarmConfig(const Json &value, const AlarmRule &motion_fallback,
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
                              AlarmSource::kAiDetection, ai_fallback, false,
                              &parsed_ai)) {
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

event::Event MakeAlarmEvent(event::EventType type, AlarmSource source,
                            const std::string &msg, int32_t value,
                            uint8_t level) {
    event::Event event;
    event.type = type;
    event.source = "alarm";
    event.target = AlarmSourceToString(source);
    event.msg = msg;
    event.value = value;
    event.timestamp_ms = infra::Time::SystemTimeMillis();
    event.level = level;
    return event;
}

class AlarmImpl : public IAlarm {
public:
    explicit AlarmImpl(const AlarmOptions &options) : options_(options) {
        for (std::size_t i = 0; i < kAlarmSourcesTotal; ++i) {
            rules_[i] = MakeDefaultRule(kAlarmSources[i]);
            source_states_[i] = MakeDefaultSourceState(kAlarmSources[i]);
        }
        for (const AlarmRule &rule : options.default_rules) {
            std::size_t index = 0;
            if (!AlarmSourceIndex(rule.source, &index)) {
                invalid_default_rule_ = true;
                continue;
            }
            rules_[index] = rule;
            SyncRuleStateLocked(index);
        }
    }

    ~AlarmImpl() override { ReleaseInternal(); }

    bool Prepare() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) {
                return true;
            }
            if (invalid_default_rule_) {
                return false;
            }
            for (const AlarmRule &rule : rules_) {
                if (!IsRuleValid(rule)) {
                    return false;
                }
            }
        }

        if (options_.config != nullptr && !IsConfigAttached()) {
            ConfigScope config_scope;
            config_scope.verify = [this](const Json &now,
                                         ConfigError *error) {
                std::lock_guard<std::mutex> guard(mutex_);
                if (VerifyConfigLocked(now)) {
                    return ConfigCode::kOk;
                }
                if (error != nullptr) {
                    error->field.clear();
                    error->message = "invalid alarm config";
                }
                return ConfigCode::kVerify;
            };
            config_scope.apply = [this](const Json &prev,
                                        const Json &now,
                                        ConfigError *error) {
                (void)prev;
                std::vector<event::Event> events;
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    if (!ApplyConfigLocked(now, &events)) {
                        if (error != nullptr) {
                            error->field.clear();
                            error->message = "apply alarm config failed";
                        }
                        return ConfigCode::kApply;
                    }
                }
                PublishEvents(events);
                return ConfigCode::kOk;
            };
            if (!options_.config->AddScope("alarm", config_scope)) {
                return false;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            config_attached_ = true;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }

        Json alarm_config;
        if (options_.config != nullptr) {
            alarm_config = options_.config->Get("alarm");
        }

        std::vector<event::Event> events;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!alarm_config.is_null() &&
                !ApplyConfigLocked(alarm_config, &events)) {
                return false;
            }
            started_ = true;
        }
        PublishEvents(events);
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

    void Release() { ReleaseInternal(); }

    AlarmInfo GetAlarmInfo() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return AlarmInfo();
        }
        return BuildAlarmInfoLocked();
    }

    bool UpdateRules(const live_stream::RequestContext &context,
                     const std::vector<AlarmRule> &rules) override {
        if (rules.size() > kAlarmSourcesTotal) {
            RecordAudit(context, OperationResult::kRejected, "alarm",
                        "too_many_rules");
            return false;
        }

        std::array<AlarmRule, kAlarmSourcesTotal> next_rules;
        for (std::size_t i = 0; i < kAlarmSourcesTotal; ++i) {
            next_rules[i] = MakeDefaultRule(kAlarmSources[i]);
        }
        for (const AlarmRule &rule : rules) {
            std::size_t index = 0;
            if (!IsRuleValid(rule) || !AlarmSourceIndex(rule.source, &index)) {
                RecordAudit(context, OperationResult::kRejected,
                            AlarmSourceToString(rule.source), "invalid_rule");
                return false;
            }
            next_rules[index] = rule;
        }

        std::vector<event::Event> events;
        bool service_not_started = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_) {
                service_not_started = true;
            } else {
                rules_ = next_rules;
                for (std::size_t i = 0; i < kAlarmSourcesTotal; ++i) {
                    SyncRuleStateLocked(i);
                    if (!rules_[i].enabled) {
                        ClearSourceLocked(i, &events);
                    }
                }
            }
        }
        if (service_not_started) {
            RecordAudit(context, OperationResult::kFailed, "alarm",
                        "service_not_started");
            return false;
        }
        PublishEvents(events);
        RecordAudit(context, OperationResult::kSuccess, "alarm", "");
        return true;
    }

    bool EnableRule(const live_stream::RequestContext &context,
                    AlarmSource source, bool enabled) override {
        std::size_t index = 0;
        if (!AlarmSourceIndex(source, &index)) {
            RecordAudit(context, OperationResult::kRejected,
                        AlarmSourceToString(source), "invalid_source");
            return false;
        }

        std::vector<event::Event> events;
        bool service_not_started = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_) {
                service_not_started = true;
            } else {
                rules_[index].enabled = enabled;
                SyncRuleStateLocked(index);
                if (!enabled) {
                    ClearSourceLocked(index, &events);
                }
            }
        }
        if (service_not_started) {
            RecordAudit(context, OperationResult::kFailed,
                        AlarmSourceToString(source), "service_not_started");
            return false;
        }
        PublishEvents(events);
        RecordAudit(context, OperationResult::kSuccess,
                    AlarmSourceToString(source), "");
        return true;
    }

    bool InjectAlarmInput(const AlarmInput &input) override {
        if (input.message.size() > kMaxAlarmMessageLength) {
            return false;
        }
        std::size_t index = 0;
        if (!AlarmSourceIndex(input.source, &index)) {
            return false;
        }

        std::vector<event::Event> events;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_) {
                return false;
            }
            ApplyAlarmInputLocked(index, input, &events);
        }
        PublishEvents(events);
        return true;
    }

    bool ClearAlarm(const live_stream::RequestContext &context) override {
        std::vector<event::Event> events;
        bool service_not_started = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_) {
                service_not_started = true;
            } else {
                for (std::size_t i = 0; i < kAlarmSourcesTotal; ++i) {
                    ClearSourceLocked(i, &events);
                }
            }
        }
        if (service_not_started) {
            RecordAudit(context, OperationResult::kFailed, "alarm",
                        "service_not_started");
            return false;
        }
        PublishEvents(events);
        RecordAudit(context, OperationResult::kSuccess, "alarm", "");
        return true;
    }

private:
    void ReleaseInternal() {
        bool should_detach = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (std::size_t i = 0; i < kAlarmSourcesTotal; ++i) {
                source_states_[i] = MakeDefaultSourceState(kAlarmSources[i]);
                SyncRuleStateLocked(i);
            }
            started_ = false;
            initialized_ = false;
            should_detach = config_attached_;
            config_attached_ = false;
        }
        if (should_detach && options_.config != nullptr) {
            static_cast<void>(options_.config->RemoveScope("alarm"));
        }
    }

    bool IsConfigAttached() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_attached_;
    }

    AlarmRule CurrentRuleLocked(AlarmSource source) const {
        std::size_t index = 0;
        if (!AlarmSourceIndex(source, &index)) {
            return AlarmRule();
        }
        return rules_[index];
    }

    bool VerifyConfigLocked(const Json &value) const {
        AlarmRule motion_rule;
        AlarmRule ai_rule;
        return ParseAlarmConfig(value, CurrentRuleLocked(AlarmSource::kMotion),
                                CurrentRuleLocked(AlarmSource::kAiDetection),
                                &motion_rule, &ai_rule);
    }

    bool ApplyConfigLocked(const Json &value,
                           std::vector<event::Event> *events) {
        AlarmRule motion_rule;
        AlarmRule ai_rule;
        if (!ParseAlarmConfig(value, CurrentRuleLocked(AlarmSource::kMotion),
                              CurrentRuleLocked(AlarmSource::kAiDetection),
                              &motion_rule, &ai_rule)) {
            return false;
        }
        SetRuleLocked(motion_rule, events);
        SetRuleLocked(ai_rule, events);
        return true;
    }

    void SetRuleLocked(const AlarmRule &rule, std::vector<event::Event> *events) {
        std::size_t index = 0;
        if (!AlarmSourceIndex(rule.source, &index)) {
            return;
        }
        rules_[index] = rule;
        SyncRuleStateLocked(index);
        if (!rule.enabled) {
            ClearSourceLocked(index, events);
        }
    }

    void SyncRuleStateLocked(std::size_t index) {
        source_states_[index].source = rules_[index].source;
        source_states_[index].enabled = rules_[index].enabled;
        source_states_[index].level = rules_[index].level;
    }

    void ApplyAlarmInputLocked(std::size_t index, const AlarmInput &input,
                               std::vector<event::Event> *events) {
        const AlarmRule &rule = rules_[index];
        AlarmSourceState &state = source_states_[index];
        SyncRuleStateLocked(index);

        if (!rule.enabled) {
            ClearSourceLocked(index, events);
            return;
        }

        if (!input.active) {
            state.waiting = false;
            state.waiting_since_ms = 0;
            if (state.active && !rule.manual_clear) {
                ClearSourceLocked(index, events);
            }
            return;
        }

        const int64_t now = infra::Time::MonotonicMillis();
        if (!state.waiting) {
            state.waiting = true;
            state.waiting_since_ms = now;
        }
        state.message = input.message;

        if (now - state.waiting_since_ms <
            static_cast<int64_t>(rule.min_duration_ms)) {
            return;
        }

        if (state.active) {
            return;
        }

        const int64_t system_now = infra::Time::SystemTimeMillis();
        if (state.last_alarm_time_ms > 0 &&
            system_now - state.last_alarm_time_ms <
                static_cast<int64_t>(rule.repeat_interval_ms)) {
            return;
        }

        state.waiting = false;
        state.waiting_since_ms = 0;
        state.active = true;
        state.active_since_ms = system_now;
        state.last_alarm_time_ms = system_now;
        state.level = rule.level;
        events->push_back(MakeAlarmEvent(event::EventType::kAlarmOn, state.source,
                                         state.message, input.value,
                                         state.level));
    }

    void ClearSourceLocked(std::size_t index, std::vector<event::Event> *events) {
        AlarmSourceState &state = source_states_[index];
        const bool was_active = state.active;
        const std::string previous_msg = state.message;
        const uint8_t previous_level = state.level;

        state.waiting = false;
        state.waiting_since_ms = 0;
        state.active = false;
        state.active_since_ms = 0;
        state.message.clear();
        SyncRuleStateLocked(index);

        if (was_active && events != nullptr) {
            events->push_back(MakeAlarmEvent(event::EventType::kAlarmOff, state.source,
                                             previous_msg, 0,
                                             previous_level));
        }
    }

    AlarmInfo BuildAlarmInfoLocked() const {
        AlarmInfo alarm_info;
        alarm_info.sources.reserve(kAlarmSourcesTotal);

        bool found_active_source = false;
        int64_t newest_alarm_time_ms = 0;
        for (const AlarmSourceState &state : source_states_) {
            alarm_info.sources.push_back(state);
            if (!state.active) {
                continue;
            }
            if (!found_active_source ||
                state.last_alarm_time_ms >= newest_alarm_time_ms) {
                found_active_source = true;
                newest_alarm_time_ms = state.last_alarm_time_ms;
                alarm_info.active = true;
                alarm_info.source = state.source;
                alarm_info.active_since_ms = state.active_since_ms;
                alarm_info.last_trigger_time_ms = state.last_alarm_time_ms;
                alarm_info.level = state.level;
                alarm_info.message = state.message;
            }
        }
        return alarm_info;
    }

    void PublishEvents(const std::vector<event::Event> &events) {
        if (options_.event == nullptr) {
            return;
        }
        for (const event::Event &event : events) {
            static_cast<void>(options_.event->Publish(event));
        }
    }

    void RecordAudit(const live_stream::RequestContext &context,
                     OperationResult result, const std::string &target,
                     const std::string &reason) {
        if (options_.logger == nullptr) {
            return;
        }
        OperationRecord record;
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = "alarm";
        record.action = OperationAction::kModifyConfig;
        record.target = target;
        record.result = result;
        record.reason = reason;
        static_cast<void>(options_.logger->RecordOperation(record));
    }

    AlarmOptions options_;
    std::array<AlarmRule, kAlarmSourcesTotal> rules_;
    std::array<AlarmSourceState, kAlarmSourcesTotal> source_states_;
    mutable std::mutex mutex_;
    bool invalid_default_rule_ = false;
    bool config_attached_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IAlarm>
CreateAlarm(const AlarmOptions &options) {
    return std::unique_ptr<IAlarm>(new AlarmImpl(options));
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
