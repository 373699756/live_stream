// handlers/config_handler.cpp.inc
// Config handlers: /api/config/*

bool IsWrappedConfigPayload(const std::string &name, const ConfigJson &value) {
    return value.is_object() && value.size() == 1 && value.contains(name) &&
           value.at(name).is_object();
}

std::string FormatConfigError(const ConfigError &error) {
    if (error.reason.empty()) {
        return "set config failed";
    }
    if (error.field.empty()) {
        return error.reason;
    }
    return error.field + ": " + error.reason;
}

HttpResponse HandleConfig(const HttpRequest &request) {
    const std::string name =
        request.path.substr(std::string("/api/config/").size());
    if (name.empty()) {
        return StatusResponse(400, "Missing config name");
    }
    if (request.method == HttpMethod::kGet) {
        AuthPrincipal principal = Authenticate(request);
        if (principal.user_name.empty()) {
            return StatusResponse(401, "Unauthorized");
        }
        ConfigJson config = dependencies_.config_service->GetValue(name);
        if (config.is_null()) {
            return StatusResponse(404, "Not Found");
        }
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "application/json";
        response.body = config.dump();
        return response;
    }
    if (request.method == HttpMethod::kPut) {
        AuthPrincipal principal;
        if (!RequirePermission(request, AuthPermission::kModifyConfig, name,
                               &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        ConfigJson config = ConfigJson::parse(request.body, nullptr, false);
        if (config.is_discarded()) {
            return StatusResponse(400, "Invalid JSON");
        }
        if (!config.is_object()) {
            return StatusResponse(400, "Config payload must be an object");
        }
        if (IsWrappedConfigPayload(name, config)) {
            return StatusResponse(400, "Config payload must be the top-level node");
        }
        bool ok = dependencies_.config_service->SetValue(name, config);
        std::string failure_reason;
        if (!ok) {
            failure_reason = FormatConfigError(
                dependencies_.config_service->GetLastConfigError(name));
        }
        RecordOperation(request, principal, OperationAction::kModifyConfig, name,
                        ok ? OperationResult::kSuccess : OperationResult::kFailed,
                        ok ? std::string() : failure_reason);
        if (!ok) {
            return StatusResponse(400, failure_reason);
        }
        return OkResponse();
    }
    return StatusResponse(404, "Not Found");
}
