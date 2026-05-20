#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "live_stream/json_utils.h"
#include "time_service.h"

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
    if (!json_utils::Load(value, "enabled", &parsed.enabled) ||
        !json_utils::LoadStringArray(value, "servers", &parsed.servers) ||
        !json_utils::Load(value, "sync_interval_sec", &parsed.sync_interval_sec,
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

ITimeService *RequireTimeService(HttpHandlerContext *context) {
    return context->Dependencies().time_service;
}

bool RequireTimePermission(HttpHandlerContext *context,
                           const HttpRequest &request,
                           AuthPermission permission,
                           AuthPrincipal *principal) {
    return RequirePermissionOrForbidden(context, request, permission, "time",
                                        principal);
}

}  // namespace

HttpResponse http_handlers::HandleTimeStatus(HttpHandlerContext *context, const HttpRequest &request) {
    ITimeService *time_service = RequireTimeService(context);
    if (time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireTimePermission(context, request, AuthPermission::kReadStatus,
                               &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return JsonResponse(200, TimeStatusToJson(time_service->GetTimeStatus()));
}

HttpResponse http_handlers::HandleTimeTimezone(HttpHandlerContext *context, const HttpRequest &request) {
    ITimeService *time_service = RequireTimeService(context);
    if (time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireTimePermission(context, request, AuthPermission::kModifyConfig,
                               &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body;
    if (!ParseJsonObject(request, &body)) {
        return StatusResponse(400, "Invalid JSON");
    }
    std::string timezone;
    if (!json_utils::Load(body, "timezone", &timezone)) {
        return StatusResponse(400, "Invalid time request");
    }
    return time_service->SetTimezone(
               context->MakeContext(request, &principal), timezone)
               ? OkResponse()
               : StatusResponse(400, "Could not set timezone");
}

HttpResponse http_handlers::HandleTimeNtp(HttpHandlerContext *context, const HttpRequest &request) {
    ITimeService *time_service = RequireTimeService(context);
    if (time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireTimePermission(context, request, AuthPermission::kModifyConfig,
                               &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body;
    if (!ParseJsonObject(request, &body)) {
        return StatusResponse(400, "Invalid JSON");
    }
    NtpConfig config;
    if (!NtpConfigFromJson(body, &config)) {
        return StatusResponse(400, "Invalid NTP config");
    }
    return time_service->UpdateNtpConfig(
               context->MakeContext(request, &principal), config)
               ? OkResponse()
               : StatusResponse(400, "Could not update NTP config");
}

HttpResponse http_handlers::HandleTimeSystemTime(HttpHandlerContext *context, const HttpRequest &request) {
    ITimeService *time_service = RequireTimeService(context);
    if (time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireTimePermission(context, request, AuthPermission::kModifyConfig,
                               &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body;
    if (!ParseJsonObject(request, &body)) {
        return StatusResponse(400, "Invalid JSON");
    }
    int64_t system_time_ms = 0;
    if (!json_utils::Load(body, "system_time_ms", &system_time_ms, 1,
                          std::numeric_limits<int64_t>::max())) {
        return StatusResponse(400, "Invalid time request");
    }
    return time_service->SetSystemTime(
               context->MakeContext(request, &principal), system_time_ms,
               TimeSyncSource::kManual)
               ? OkResponse()
               : StatusResponse(503, "Could not set system time");
}

HttpResponse http_handlers::HandleTimeSync(HttpHandlerContext *context, const HttpRequest &request) {
    ITimeService *time_service = RequireTimeService(context);
    if (time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireTimePermission(context, request, AuthPermission::kModifyConfig,
                               &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return time_service->SyncNow(context->MakeContext(request, &principal),
                                 TimeSyncSource::kNtp)
               ? OkResponse()
               : StatusResponse(503, "Could not sync time");
}

}  // namespace live_stream
