#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "json_utils.h"
#include "time_api.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace {

ConfigJson NtpConfigToJson(const NtpConfig &config) {
    ConfigJson root = ConfigJson::object();
    root["enabled"] = config.enabled;
    ConfigJson servers = ConfigJson::array();
    for (const std::string &server : config.servers) {
        servers.push_back(server);
    }
    root["servers"] = servers;
    root["sync_interval_sec"] = config.sync_interval_sec;
    return root;
}

bool NtpConfigFromJson(const ConfigJson &value, NtpConfig *config) {
    if (config == nullptr || !value.is_object()) {
        return false;
    }
    NtpConfig parsed;
    if (!json_utils::ReadField(value, "enabled", &parsed.enabled) ||
        !json_utils::ReadStringArray(value, "servers", &parsed.servers) ||
        !json_utils::ReadField(value, "sync_interval_sec", &parsed.sync_interval_sec,
                          1, 0xffffffffU)) {
        return false;
    }
    *config = parsed;
    return true;
}

ConfigJson TimeStatusToJson(const TimeStatus &status) {
    ConfigJson root = ConfigJson::object();
    root["system_time_ms"] = status.system_time_ms;
    root["timezone"] = status.timezone;
    root["ntp"] = NtpConfigToJson(status.ntp);
    root["last_sync_source"] = TimeSyncSourceToString(status.last_sync_source);
    root["last_sync_time_ms"] = status.last_sync_time_ms;
    root["last_sync_ok"] = status.last_sync_ok;
    return root;
}

bool RequireTimePermission(HttpAccess *access,
                           const HttpRequest &request,
                           AuthPermission permission,
                           AuthPrincipal *principal) {
    return RequirePermissionOrForbidden(access, request, permission, "time",
                                        principal);
}

}  // namespace

class TimeHttpHandler : public IHttpHandler {
public:
    TimeHttpHandler(HttpAccess *access, ITime *time)
        : access_(access), time_(time) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/time/status",
                              &TimeHttpHandler::HandleStatusRoute, this);
        router->AddExactRoute(HttpMethod::kPut, "/api/time/timezone",
                              &TimeHttpHandler::HandleTimezoneRoute, this);
        router->AddExactRoute(HttpMethod::kPut, "/api/time/ntp",
                              &TimeHttpHandler::HandleNtpRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/time/system-time",
                              &TimeHttpHandler::HandleSystemTimeRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/time/sync",
                              &TimeHttpHandler::HandleSyncRoute, this);
    }

private:
    static HttpResponse HandleStatusRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<TimeHttpHandler *>(user)->HandleStatus(request);
    }

    static HttpResponse HandleTimezoneRoute(void *user,
                                            const HttpRequest &request) {
        return static_cast<TimeHttpHandler *>(user)->HandleTimezone(request);
    }

    static HttpResponse HandleNtpRoute(void *user,
                                       const HttpRequest &request) {
        return static_cast<TimeHttpHandler *>(user)->HandleNtp(request);
    }

    static HttpResponse HandleSystemTimeRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<TimeHttpHandler *>(user)->HandleSystemTime(request);
    }

    static HttpResponse HandleSyncRoute(void *user,
                                        const HttpRequest &request) {
        return static_cast<TimeHttpHandler *>(user)->HandleSync(request);
    }

    HttpResponse HandleStatus(const HttpRequest &request) {
        ITime *time = time_;
        if (time == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireTimePermission(access_, request,
                                   AuthPermission::kReadStatus, &principal)) {
            return ForbiddenResponse(principal);
        }
        return JsonResponse(200,
                            TimeStatusToJson(time->GetTimeStatus()));
    }

    HttpResponse HandleTimezone(const HttpRequest &request) {
        ITime *time = time_;
        if (time == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireTimePermission(access_, request,
                                   AuthPermission::kModifyConfig,
                                   &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        std::string timezone;
        if (!json_utils::ReadField(body, "timezone", &timezone)) {
            return StatusResponse(400, "Invalid time request");
        }
        return time
                       ->SetTimezone(access_->MakeContext(request, &principal), timezone)
                   ? OkResponse()
                   : StatusResponse(400, "Could not set timezone");
    }

    HttpResponse HandleNtp(const HttpRequest &request) {
        ITime *time = time_;
        if (time == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireTimePermission(access_, request,
                                   AuthPermission::kModifyConfig,
                                   &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        NtpConfig config;
        if (!NtpConfigFromJson(body, &config)) {
            return StatusResponse(400, "Invalid NTP config");
        }
        return time
                       ->UpdateNtpConfig(access_->MakeContext(request, &principal),
                                         config)
                   ? OkResponse()
                   : StatusResponse(400, "Could not update NTP config");
    }

    HttpResponse HandleSystemTime(const HttpRequest &request) {
        ITime *time = time_;
        if (time == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireTimePermission(access_, request,
                                   AuthPermission::kModifyConfig,
                                   &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        int64_t system_time_ms = 0;
        if (!json_utils::ReadField(body, "system_time_ms", &system_time_ms, 1,
                              std::numeric_limits<int64_t>::max())) {
            return StatusResponse(400, "Invalid time request");
        }
        return time
                       ->SetSystemTime(access_->MakeContext(request, &principal),
                                       system_time_ms, TimeSyncSource::kManual)
                   ? OkResponse()
                   : StatusResponse(503, "Could not set system time");
    }

    HttpResponse HandleSync(const HttpRequest &request) {
        ITime *time = time_;
        if (time == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireTimePermission(access_, request,
                                   AuthPermission::kModifyConfig,
                                   &principal)) {
            return ForbiddenResponse(principal);
        }
        return time
                       ->SyncNow(access_->MakeContext(request, &principal),
                                 TimeSyncSource::kNtp)
                   ? OkResponse()
                   : StatusResponse(503, "Could not sync time");
    }

    HttpAccess *access_ = nullptr;
    ITime *time_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateTimeHttpHandler(
    HttpAccess *access, ITime *time) {
    return std::unique_ptr<IHttpHandler>(
        new TimeHttpHandler(access, time));
}

}  // namespace live_stream
