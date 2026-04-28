#include "alarm_service.h"

#include "event_service.h"

#include <memory>
#include <string>
#include <vector>

namespace {

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

    infra::RequestContext context;
    if (service->EnableRule(context, live_stream::AlarmSource::kMotion, true) !=
        infra::Status::kOk) {
        return 3;
    }
    if (service->InjectAlarmInput(input) != infra::Status::kOk ||
        event_service.publish_count != 1 ||
        event_service.last_event.type != live_stream::EventType::kAlarmTriggered ||
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
    if (service->UpdateRules(context, std::vector<live_stream::AlarmRule>{rule}) !=
        infra::Status::kOk) {
        return 8;
    }

    service->Stop();
    service->Deinit();
    return 0;
}
