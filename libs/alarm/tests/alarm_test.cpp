#include "alarm.h"

#include "config.h"
#include "event.h"
#include "infra/time.h"
#include "logger.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeConfig : public live_stream::IConfig {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::ConfigStatus Set(const std::string& name,
                                  const live_stream::ConfigJson& now,
                                  live_stream::ConfigIssue* issue) override {
        if (name != "alarm") {
            return live_stream::ConfigStatus::kNotFound;
        }
        if (scope.verify) {
            const live_stream::ConfigStatus status = scope.verify(now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        if (scope.apply) {
            const live_stream::ConfigStatus status =
                scope.apply(alarm_config, now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        alarm_config = now;
        return live_stream::ConfigStatus::kOk;
    }

    live_stream::ConfigJson Get(const std::string& name) override {
        if (name != "alarm") {
            return live_stream::ConfigJson();
        }
        return alarm_config;
    }

    live_stream::ConfigStatus Reset(
        const std::string& name, live_stream::ConfigIssue*) override {
        return name == "alarm" ? live_stream::ConfigStatus::kOk
                               : live_stream::ConfigStatus::kNotFound;
    }

    live_stream::ConfigJson Default(const std::string& name) override {
        if (name != "alarm") {
            return live_stream::ConfigJson();
        }
        return alarm_config;
    }

    live_stream::ConfigStatus ResetAll(
        live_stream::ConfigIssue*) override {
        return live_stream::ConfigStatus::kOk;
    }

    bool AddScope(const std::string& name,
                  const live_stream::ConfigScope& next) override {
        if (name != "alarm") {
            return false;
        }
        scope = next;
        return true;
    }

    bool RemoveScope(const std::string& name) override {
        if (name != "alarm") {
            return false;
        }
        scope = live_stream::ConfigScope();
        return true;
    }

    live_stream::ConfigJson alarm_config = {
        {"motion_detection",
         {{"enabled", false},
          {"sensitivity", 50},
          {"min_duration_ms", 100},
          {"regions", live_stream::ConfigJson::array()}}},
        {"actions", {{"snapshot", true}, {"record", false}, {"notify", true}}},
        {"schedule",
         {{"mode", "always"}, {"weekly", live_stream::ConfigJson::array()}}}};
    live_stream::ConfigScope scope;
};

class FakeEvent : public live_stream::event::Dispatcher {
public:
    FakeEvent()
        : subscription_(SubscribeMany(
              std::vector<live_stream::event::EventType>{
                  live_stream::event::EventType::kAlarmOn,
                  live_stream::event::EventType::kAlarmOff},
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
    bool RecordOperation(const live_stream::OperationRecord& record) override {
        records.push_back(record);
        return true;
    }
    std::vector<live_stream::OperationRecord> QueryOperations(
        const live_stream::OperationLogQuery&) override {
        return records;
    }
    bool ExportOperations(
        const live_stream::OperationLogExportOptions&) override {
        return true;
    }

    std::vector<live_stream::OperationRecord> records;
};

}  // namespace

int main() {
    if (std::string(live_stream::AlarmSourceToString(
            live_stream::AlarmSource::kNetwork)) != "network") {
        return 1;
    }

    FakeEvent event;
    live_stream::AlarmOptions options;
    options.event = &event;
    options.default_rules.push_back(
        live_stream::AlarmRule{live_stream::AlarmSource::kMotion, false, 0});

    std::unique_ptr<live_stream::IAlarm> service =
        live_stream::CreateAlarm(options);
    if (!service || !service->Start() || !service->IsStarted()) {
        return 2;
    }

    live_stream::AlarmInput input;
    input.source = live_stream::AlarmSource::kMotion;
    input.active = true;
    input.message = "motion";
    if (!service->InjectAlarmInput(input) || event.publish_count != 0) {
        return 3;
    }

    live_stream::RequestContext context;
    if (!service->EnableRule(context, live_stream::AlarmSource::kMotion, true)) {
        return 4;
    }
    if (!service->InjectAlarmInput(input) || event.publish_count != 1 ||
        event.last_event.type != live_stream::event::EventType::kAlarmOn ||
        event.last_event.target != "motion" || event.last_event.level != 1) {
        return 5;
    }

    live_stream::AlarmStatus status = service->GetAlarmStatus();
    if (!status.active || status.source != live_stream::AlarmSource::kMotion ||
        status.sources.size() != 5U) {
        return 6;
    }

    if (!service->ClearAlarm(context)) {
        return 7;
    }
    if (service->GetAlarmStatus().active || event.publish_count != 2 ||
        event.last_event.type != live_stream::event::EventType::kAlarmOff) {
        return 8;
    }

    live_stream::AlarmRule network_rule;
    network_rule.source = live_stream::AlarmSource::kNetwork;
    network_rule.enabled = true;
    network_rule.min_duration_ms = 0;
    if (!service->UpdateRules(context, std::vector<live_stream::AlarmRule>{
                                           network_rule})) {
        return 9;
    }

    service->Stop();
    if (service->IsStarted()) {
        return 10;
    }

    FakeConfig config;
    FakeEvent config_event;
    FakeLogger logger;
    live_stream::AlarmOptions config_options;
    config_options.config = &config;
    config_options.event = &config_event;
    config_options.logger = &logger;
    std::unique_ptr<live_stream::IAlarm> configured =
        live_stream::CreateAlarm(config_options);
    if (!configured || !configured->Start()) {
        return 11;
    }

    config.alarm_config["motion_detection"]["enabled"] = true;
    config.alarm_config["motion_detection"]["min_duration_ms"] = 0;
    if (config.Set("alarm", config.alarm_config, nullptr) !=
        live_stream::ConfigStatus::kOk) {
        return 12;
    }

    input.message = "configured-motion";
    if (!configured->InjectAlarmInput(input) ||
        config_event.publish_count != 1) {
        return 13;
    }
    if (configured->GetAlarmStatus().message != "configured-motion") {
        return 14;
    }

    const int log_count = static_cast<int>(logger.records.size());
    context.request_id = "alarm-1";
    context.user_name = "admin";
    context.session_id = "session-1";
    context.client_ip = "127.0.0.1";
    if (!configured->EnableRule(context, live_stream::AlarmSource::kNetwork,
                                true)) {
        return 15;
    }
    if (static_cast<int>(logger.records.size()) != log_count + 1 ||
        logger.records.back().module != "alarm" ||
        logger.records.back().target != "network") {
        return 16;
    }

    if (!configured->ClearAlarm(context) ||
        static_cast<int>(logger.records.size()) != log_count + 2) {
        return 17;
    }

    configured->Stop();
    return configured->IsStarted() ? 18 : 0;
}
