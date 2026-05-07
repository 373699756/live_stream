#include "alarm_service.h"

#include "config_service.h"
#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeConfigService : public live_stream::IConfigService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_config"; }

    infra::Status SetValue(const std::string& name,
                           const live_stream::ConfigJson& value) override {
        if (name != "alarm") {
            return infra::Status::kInvalidParam;
        }
        if (verify && verify(value) != infra::Status::kOk) {
            return infra::Status::kInvalidParam;
        }
        if (apply) {
            const infra::Status status = apply(value);
            if (status != infra::Status::kOk) {
                return status;
            }
        }
        alarm_config = value;
        return infra::Status::kOk;
    }

    infra::Status GetValue(const std::string& name,
                           live_stream::ConfigJson* value) override {
        if (name != "alarm" || value == nullptr) {
            return infra::Status::kInvalidParam;
        }
        *value = alarm_config;
        return infra::Status::kOk;
    }

    infra::Status GetDefault(const std::string&,
                             live_stream::ConfigJson*) override {
        return infra::Status::kNotFound;
    }

    infra::Status RestoreDefaults() override { return infra::Status::kOk; }
    infra::Status SaveFile() override { return infra::Status::kOk; }

    infra::Status RegisterApply(const std::string& name,
                                live_stream::ConfigProc proc) override {
        if (name != "alarm") {
            return infra::Status::kInvalidParam;
        }
        apply = proc;
        return infra::Status::kOk;
    }

    infra::Status RegisterVerify(const std::string& name,
                                 live_stream::ConfigProc proc) override {
        if (name != "alarm") {
            return infra::Status::kInvalidParam;
        }
        verify = proc;
        return infra::Status::kOk;
    }

    live_stream::ConfigJson alarm_config = {
        {"motion_detection",
         {{"enabled", false},
          {"sensitivity", 50},
          {"min_duration_ms", 100},
          {"regions", live_stream::ConfigJson::array()}}},
        {"actions", {{"snapshot", true}, {"record", false}, {"notify", true}}},
        {"schedule", {{"mode", "always"},
                      {"weekly", live_stream::ConfigJson::array()}}}};
    live_stream::ConfigProc verify;
    live_stream::ConfigProc apply;
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
        records.push_back(record);
        return infra::Status::kOk;
    }

    infra::Result<std::vector<live_stream::OperationRecord>> QueryOperations(
        const live_stream::OperationLogQuery&) override {
        return infra::Result<std::vector<live_stream::OperationRecord>>::Ok(
            records);
    }

    infra::Status ExportOperations(
        const live_stream::OperationLogExportOptions&) override {
        return infra::Status::kOk;
    }

    std::vector<live_stream::OperationRecord> records;
};

}  // namespace

int main() {
    FakeEventService event_service;
    live_stream::AlarmRule rule;
    rule.source = live_stream::AlarmSource::kMotion;
    rule.enabled = false;
    rule.min_duration_ms = 0;

    live_stream::AlarmServiceOptions options;
    options.event_service = &event_service;
    options.default_rules.push_back(rule);
    std::unique_ptr<live_stream::IAlarmService> service =
        live_stream::CreateAlarmService(options);
    if (!service || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk ||
        std::string(service->Name()) != "alarm_service") {
        return 1;
    }

    live_stream::AlarmInput input;
    input.source = live_stream::AlarmSource::kMotion;
    input.active = true;
    input.message = "motion";
    if (service->InjectAlarmInput(input) != infra::Status::kOk ||
        event_service.publish_count != 0) {
        return 2;
    }

    live_stream::RequestContext context;
    if (service->EnableRule(context, live_stream::AlarmSource::kMotion, true) !=
        infra::Status::kOk) {
        return 3;
    }
    if (service->InjectAlarmInput(input) != infra::Status::kOk ||
        event_service.publish_count != 1 ||
        event_service.last_event.type !=
            live_stream::EventType::kAlarmTriggered ||
        event_service.last_event.target != "motion") {
        return 4;
    }

    infra::Result<live_stream::AlarmStatus> status = service->GetAlarmStatus();
    if (!status.IsOk() || !status.value.active ||
        status.value.source != live_stream::AlarmSource::kMotion) {
        return 5;
    }

    if (service->ClearAlarm(context) != infra::Status::kOk) {
        return 6;
    }
    status = service->GetAlarmStatus();
    if (!status.IsOk() || status.value.active) {
        return 7;
    }

    rule.enabled = true;
    rule.source = live_stream::AlarmSource::kNetwork;
    if (service->UpdateRules(
            context, std::vector<live_stream::AlarmRule>{rule}) !=
        infra::Status::kOk) {
        return 8;
    }

    service->Stop();
    service->Deinit();

    FakeConfigService config_service;
    FakeEventService config_event_service;
    FakeLoggerService logger_service;
    live_stream::AlarmServiceOptions config_options;
    config_options.config_service = &config_service;
    config_options.event_service = &config_event_service;
    config_options.logger_service = &logger_service;
    std::unique_ptr<live_stream::IAlarmService> configured =
        live_stream::CreateAlarmService(config_options);
    if (!configured || configured->Init() != infra::Status::kOk ||
        configured->Start() != infra::Status::kOk) {
        return 9;
    }

    input.source = live_stream::AlarmSource::kMotion;
    input.active = true;
    input.message = "configured-motion";
    if (configured->InjectAlarmInput(input) != infra::Status::kOk ||
        config_event_service.publish_count != 0) {
        return 10;
    }

    config_service.alarm_config["motion_detection"]["enabled"] = true;
    config_service.alarm_config["motion_detection"]["min_duration_ms"] = 50;
    if (config_service.SetValue("alarm", config_service.alarm_config) !=
        infra::Status::kOk) {
        return 11;
    }
    if (configured->InjectAlarmInput(input) != infra::Status::kOk ||
        config_event_service.publish_count != 0) {
        return 12;
    }
    infra::Time::SleepMillis(60);
    if (configured->InjectAlarmInput(input) != infra::Status::kOk ||
        config_event_service.publish_count != 1 ||
        config_event_service.last_event.target != "motion") {
        return 13;
    }
    if (configured->InjectAlarmInput(input) != infra::Status::kOk ||
        config_event_service.publish_count != 1) {
        return 14;
    }

    config_service.alarm_config["motion_detection"]["enabled"] = false;
    if (config_service.SetValue("alarm", config_service.alarm_config) !=
        infra::Status::kOk) {
        return 15;
    }
    status = configured->GetAlarmStatus();
    if (!status.IsOk() || status.value.active) {
        return 16;
    }

    live_stream::ConfigJson invalid = config_service.alarm_config;
    invalid["motion_detection"]["enabled"] = true;
    invalid["motion_detection"]["min_duration_ms"] = -1;
    if (config_service.SetValue("alarm", invalid) !=
        infra::Status::kInvalidParam) {
        return 17;
    }
    invalid = config_service.alarm_config;
    invalid["motion_detection"]["sensitivity"] = 101;
    if (config_service.SetValue("alarm", invalid) !=
        infra::Status::kInvalidParam) {
        return 18;
    }
    invalid = config_service.alarm_config;
    invalid["actions"]["notify"] = "yes";
    if (config_service.SetValue("alarm", invalid) !=
        infra::Status::kInvalidParam) {
        return 19;
    }
    invalid = config_service.alarm_config;
    invalid["schedule"]["mode"] = "weekly";
    if (config_service.SetValue("alarm", invalid) !=
        infra::Status::kInvalidParam) {
        return 20;
    }

    const int log_count = static_cast<int>(logger_service.records.size());
    context.request_id = "alarm-1";
    context.user_name = "admin";
    context.session_id = "session-1";
    context.client_ip = "127.0.0.1";
    if (configured->EnableRule(
            context, live_stream::AlarmSource::kNetwork, true) !=
        infra::Status::kOk) {
        return 21;
    }
    if (static_cast<int>(logger_service.records.size()) != log_count + 1 ||
        logger_service.records.back().action !=
            live_stream::OperationAction::kModifyConfig ||
        logger_service.records.back().module != "alarm_service" ||
        logger_service.records.back().target != "network") {
        return 22;
    }

    input.source = live_stream::AlarmSource::kNetwork;
    input.message = "network";
    if (configured->InjectAlarmInput(input) != infra::Status::kOk ||
        static_cast<int>(logger_service.records.size()) != log_count + 1) {
        return 23;
    }
    if (configured->ClearAlarm(context) != infra::Status::kOk ||
        static_cast<int>(logger_service.records.size()) != log_count + 2) {
        return 24;
    }
    if (configured->UpdateRules(
            context, std::vector<live_stream::AlarmRule>{rule}) !=
        infra::Status::kOk ||
        static_cast<int>(logger_service.records.size()) != log_count + 3) {
        return 25;
    }

    configured->Stop();
    configured->Deinit();
    return 0;
}
