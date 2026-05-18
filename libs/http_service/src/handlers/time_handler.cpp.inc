// handlers/time_handler.cpp.inc
// Time handlers: /api/time/*

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

HttpResponse HandleTimeStatus(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReadStatus, "time",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return JsonResponse(
        200, TimeStatusToJson(dependencies_.time_service->GetTimeStatus()));
}

HttpResponse HandleTimeTimezone(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return StatusResponse(400, "Invalid JSON");
    }
    std::string timezone;
    if (!json_utils::Load(body, "timezone", &timezone)) {
        return StatusResponse(400, "Invalid time request");
    }
    return dependencies_.time_service->SetTimezone(
               MakeContext(request, &principal), timezone)
               ? OkResponse()
               : StatusResponse(400, "Could not set timezone");
}

HttpResponse HandleTimeNtp(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return StatusResponse(400, "Invalid JSON");
    }
    NtpConfig config;
    if (!NtpConfigFromJson(body, &config)) {
        return StatusResponse(400, "Invalid NTP config");
    }
    return dependencies_.time_service->UpdateNtpConfig(
               MakeContext(request, &principal), config)
               ? OkResponse()
               : StatusResponse(400, "Could not update NTP config");
}

HttpResponse HandleTimeSystemTime(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return StatusResponse(400, "Invalid JSON");
    }
    int64_t system_time_ms = 0;
    if (!json_utils::Load(body, "system_time_ms", &system_time_ms, 1,
                          std::numeric_limits<int64_t>::max())) {
        return StatusResponse(400, "Invalid time request");
    }
    return dependencies_.time_service->SetSystemTime(
               MakeContext(request, &principal), system_time_ms,
               TimeSyncSource::kManual)
               ? OkResponse()
               : StatusResponse(503, "Could not set system time");
}

HttpResponse HandleTimeSync(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return dependencies_.time_service->SyncNow(MakeContext(request, &principal),
                                               TimeSyncSource::kNtp)
               ? OkResponse()
               : StatusResponse(503, "Could not sync time");
}
