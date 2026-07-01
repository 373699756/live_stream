#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_response.h"

#include "alarm.h"

namespace live_stream {
namespace {

Json AlarmSourceStateToJson(const AlarmSourceState &state) {
    Json root = Json::object();
    root["source"] = AlarmSourceToString(state.source);
    root["enabled"] = state.enabled;
    root["waiting"] = state.waiting;
    root["active"] = state.active;
    root["waiting_since_ms"] = state.waiting_since_ms;
    root["active_since_ms"] = state.active_since_ms;
    root["last_alarm_time_ms"] = state.last_alarm_time_ms;
    root["level"] = state.level;
    root["message"] = state.message;
    return root;
}

Json AlarmInfoToJson(const AlarmInfo &alarm_info) {
    Json root = Json::object();
    root["active"] = alarm_info.active;
    root["source"] = AlarmSourceToString(alarm_info.source);
    root["active_since_ms"] = alarm_info.active_since_ms;
    root["last_trigger_time_ms"] = alarm_info.last_trigger_time_ms;
    root["level"] = alarm_info.level;
    root["message"] = alarm_info.message;
    Json sources = Json::array();
    for (const AlarmSourceState &source : alarm_info.sources) {
        sources.push_back(AlarmSourceStateToJson(source));
    }
    root["sources"] = sources;
    return root;
}

}  // namespace

class AlarmHttpHandler : public IHttpHandler {
public:
    explicit AlarmHttpHandler(const AlarmHandlerRefs &refs)
        : access_(refs.access), alarm_(refs.alarm) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (alarm_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kGet, "/api/alarm/status", this,
                             &AlarmHttpHandler::HandleInfo);
    }

private:
    HttpResponse HandleInfo(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json root = Json::object();
        root["available"] = alarm_->IsStarted();
        root["status"] = AlarmInfoToJson(alarm_->GetAlarmInfo());
        return JsonResponse(200, root);
    }

    HttpAccess *access_ = nullptr;
    IAlarm *alarm_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAlarmHandler(
    const AlarmHandlerRefs &refs) {
    return std::unique_ptr<IHttpHandler>(
        new AlarmHttpHandler(refs));
}

}  // namespace live_stream
