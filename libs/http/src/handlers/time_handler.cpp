#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_json_body.h"
#include "http_response.h"

#include "json_reader.h"
#include "system/time.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace {

Json NtpConfigToJson(const NtpConfig &config) {
    Json root = Json::object();
    root["enabled"] = config.enabled;
    Json servers = Json::array();
    for (const std::string &server : config.servers) {
        servers.push_back(server);
    }
    root["servers"] = servers;
    root["sync_interval_sec"] = config.sync_interval_sec;
    return root;
}

bool NtpConfigFromJson(const Json &value, NtpConfig *config) {
    if (config == nullptr || !value.is_object()) {
        return false;
    }
    NtpConfig parsed;
    if (!json_reader::ReadField(value, "enabled", &parsed.enabled) ||
        !json_reader::ReadStringArray(value, "servers", &parsed.servers) ||
        !json_reader::ReadField(value, "sync_interval_sec", &parsed.sync_interval_sec,
                               1, 0xffffffffU)) {
        return false;
    }
    *config = parsed;
    return true;
}

bool TimeConfigFromJson(const Json &value, TimeConfig *config) {
    if (config == nullptr || !value.is_object()) {
        return false;
    }
    TimeConfig parsed;
    if (!json_reader::ReadField(value, "timezone", &parsed.timezone) ||
        !value.contains("ntp") || !value.at("ntp").is_object() ||
        !NtpConfigFromJson(value.at("ntp"), &parsed.ntp)) {
        return false;
    }
    if (value.contains("manual_sync_allowed") &&
        !json_reader::ReadField(value, "manual_sync_allowed",
                               &parsed.manual_sync_allowed)) {
        return false;
    }
    if (value.contains("browser_sync_on_login") &&
        !json_reader::ReadField(value, "browser_sync_on_login",
                               &parsed.browser_sync_on_login)) {
        return false;
    }
    *config = parsed;
    return true;
}

Json TimeInfoToJson(const TimeInfo &time_info) {
    Json root = Json::object();
    root["system_time_ms"] = time_info.system_time_ms;
    root["timezone"] = time_info.timezone;
    root["ntp"] = NtpConfigToJson(time_info.ntp);
    root["manual_sync_allowed"] = time_info.manual_sync_allowed;
    root["browser_sync_on_login"] = time_info.browser_sync_on_login;
    root["last_sync_source"] =
        TimeSyncSourceToString(time_info.last_sync_source);
    root["last_sync_time_ms"] = time_info.last_sync_time_ms;
    root["last_sync_ok"] = time_info.last_sync_ok;
    return root;
}

}  // namespace

class TimeHttpHandler : public IHttpHandler {
public:
    explicit TimeHttpHandler(const TimeHandlerRefs &refs)
        : access_(refs.access), time_(refs.time) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (time_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kGet, "/api/system/time/status",
                             this, &TimeHttpHandler::HandleInfo);
        router.AddExactRoute(HttpMethod::kPut, "/api/system/time/config",
                             this, &TimeHttpHandler::HandleConfig);
        router.AddExactRoute(HttpMethod::kPut, "/api/system/time/timezone",
                             this, &TimeHttpHandler::HandleTimezone);
        router.AddExactRoute(HttpMethod::kPut, "/api/system/time/ntp",
                             this, &TimeHttpHandler::HandleNtp);
        router.AddExactRoute(HttpMethod::kPost,
                             "/api/system/time/system-time", this,
                             &TimeHttpHandler::HandleSystemTime);
        router.AddExactRoute(HttpMethod::kPost,
                             "/api/system/time/browser-time", this,
                             &TimeHttpHandler::HandleBrowserTime);
        router.AddExactRoute(HttpMethod::kPut,
                             "/api/system/time/browser-sync", this,
                             &TimeHttpHandler::HandleBrowserSync);
        router.AddExactRoute(HttpMethod::kPost, "/api/system/time/sync",
                             this, &TimeHttpHandler::HandleSync);
    }

private:
    HttpResponse HandleInfo(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return JsonResponse(200, TimeInfoToJson(time_->GetTimeInfo()));
    }

    HttpResponse HandleConfig(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        TimeConfig config;
        if (!TimeConfigFromJson(body, &config)) {
            return StatusResponse(400, "Invalid time config");
        }
        return time_->UpdateTimeConfig(
                   access_->MakeContext(request, &principal), config)
                   ? OkResponse()
                   : StatusResponse(400, "Could not update time config");
    }

    HttpResponse HandleTimezone(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        std::string timezone;
        if (!json_reader::ReadField(body, "timezone", &timezone)) {
            return StatusResponse(400, "Invalid time request");
        }
        return time_->SetTimezone(
                   access_->MakeContext(request, &principal), timezone)
                   ? OkResponse()
                   : StatusResponse(400, "Could not set timezone");
    }

    HttpResponse HandleNtp(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        NtpConfig config;
        if (!NtpConfigFromJson(body, &config)) {
            return StatusResponse(400, "Invalid NTP config");
        }
        return time_->UpdateNtpConfig(
                   access_->MakeContext(request, &principal), config)
                   ? OkResponse()
                   : StatusResponse(400, "Could not update NTP config");
    }

    HttpResponse HandleSystemTime(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        int64_t system_time_ms = 0;
        if (!json_reader::ReadField(body, "system_time_ms", &system_time_ms, 1,
                                   std::numeric_limits<int64_t>::max())) {
            return StatusResponse(400, "Invalid time request");
        }
        return time_->SetSystemTime(
                   access_->MakeContext(request, &principal),
                   system_time_ms, TimeSyncSource::kManual)
                   ? OkResponse()
                   : StatusResponse(503, "Could not set system time");
    }

    HttpResponse HandleBrowserTime(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        int64_t system_time_ms = 0;
        if (!json_reader::ReadField(body, "system_time_ms", &system_time_ms, 1,
                                   std::numeric_limits<int64_t>::max())) {
            return StatusResponse(400, "Invalid time request");
        }
        return time_->SetSystemTime(
                   access_->MakeContext(request, &principal),
                   system_time_ms, TimeSyncSource::kBrowser)
                   ? OkResponse()
                   : StatusResponse(503, "Could not sync browser time");
    }

    HttpResponse HandleBrowserSync(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        bool manual_sync_allowed = true;
        bool browser_sync_on_login = true;
        if (!json_reader::ReadField(body, "manual_sync_allowed",
                                   &manual_sync_allowed) ||
            !json_reader::ReadField(body, "browser_sync_on_login",
                                   &browser_sync_on_login)) {
            return StatusResponse(400, "Invalid browser sync config");
        }
        return time_->UpdateBrowserSyncConfig(
                   access_->MakeContext(request, &principal),
                   manual_sync_allowed, browser_sync_on_login)
                   ? OkResponse()
                   : StatusResponse(400, "Could not update browser sync config");
    }

    HttpResponse HandleSync(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "time",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return time_->SyncNow(access_->MakeContext(request, &principal),
                              TimeSyncSource::kNtp)
                   ? OkResponse()
                   : StatusResponse(503, "Could not sync time");
    }

    HttpAccess *access_ = nullptr;
    ITime *time_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeTimeHandler(
    const TimeHandlerRefs &refs) {
    return std::unique_ptr<IHttpHandler>(
        new TimeHttpHandler(refs));
}

}  // namespace live_stream
