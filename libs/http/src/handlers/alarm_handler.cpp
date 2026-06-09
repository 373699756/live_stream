#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "alarm.h"

namespace live_stream {
namespace {

const char *AlarmSourceToJsonString(AlarmSource source) {
    return AlarmSourceToString(source);
}

ConfigJson AlarmStatusToJson(const AlarmStatus &status) {
    ConfigJson root = ConfigJson::object();
    root["active"] = status.active;
    root["source"] = AlarmSourceToJsonString(status.source);
    root["active_since_ms"] = status.active_since_ms;
    root["last_trigger_time_ms"] = status.last_trigger_time_ms;
    root["message"] = status.message;
    return root;
}

}  // namespace

class AlarmHttpHandler : public IHttpHandler {
public:
    AlarmHttpHandler(HttpAccess *access, IAlarm *alarm)
        : access_(access), alarm_(alarm) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/alarm/status",
                              &AlarmHttpHandler::HandleStatusRoute, this);
    }

private:
    static HttpResponse HandleStatusRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<AlarmHttpHandler *>(user)->HandleStatus(request);
    }

    HttpResponse HandleStatus(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (alarm_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }

        ConfigJson root = ConfigJson::object();
        root["available"] = alarm_->IsStarted();
        root["status"] = AlarmStatusToJson(alarm_->GetAlarmStatus());
        return JsonResponse(200, root);
    }

    HttpAccess *access_ = nullptr;
    IAlarm *alarm_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAlarmHandler(HttpAccess *access,
                                               IAlarm *alarm) {
    return std::unique_ptr<IHttpHandler>(
        new AlarmHttpHandler(access, alarm));
}

}  // namespace live_stream
