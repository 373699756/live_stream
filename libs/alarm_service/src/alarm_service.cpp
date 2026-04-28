#include "alarm_service.h"

#include "event_service.h"
#include "infra/time.h"

#include <map>
#include <mutex>

namespace live_stream {
namespace {

constexpr std::size_t kMaxRules = 16;
constexpr std::size_t kMaxAlarmMessageLength = 128;

bool IsRuleValid(const AlarmRule& rule) {
    return rule.min_duration_ms <= 60U * 60U * 1000U;
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
        (void)context;
        if (rules.size() > kMaxRules) {
            return infra::Status::kInvalidParam;
        }
        std::map<AlarmSource, AlarmRule> next_rules;
        for (const AlarmRule& rule : rules) {
            if (!IsRuleValid(rule)) {
                return infra::Status::kInvalidParam;
            }
            next_rules[rule.source] = rule;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            rules_.swap(next_rules);
            pending_since_ms_.clear();
            if (status_.active && rules_.find(status_.source) == rules_.end()) {
                status_ = AlarmStatus();
            }
        }
        return infra::Status::kOk;
    }

    infra::Status EnableRule(const infra::RequestContext& context,
                            AlarmSource source,
                            bool enabled) override {
        (void)context;
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
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
        (void)context;
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        pending_since_ms_.clear();
        status_ = AlarmStatus();
        return infra::Status::kOk;
    }

 private:
    bool IsStarted() {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
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

    AlarmServiceOptions options_;
    std::map<AlarmSource, AlarmRule> rules_;
    std::map<AlarmSource, int64_t> pending_since_ms_;
    AlarmStatus status_;
    std::mutex mutex_;
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
