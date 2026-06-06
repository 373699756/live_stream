#include "http_handler_utils.h"

#include "ai.h"
#include "config.h"
#include "json_utils.h"
#include "device_media.h"

namespace live_stream {

HttpResponse JsonResponse(int status_code, const ConfigJson &value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse StatusResponse(int status_code, const std::string &reason) {
    ConfigJson root = ConfigJson::object();
    root["error"] = reason;
    return JsonResponse(status_code, root);
}

HttpResponse ForbiddenResponse(const AuthPrincipal &principal) {
    if (principal.must_change_password) {
        return StatusResponse(403, "must_change_password");
    }
    return StatusResponse(403, "Forbidden");
}

HttpResponse OkResponse() {
    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    return JsonResponse(200, root);
}

bool RequireAuth(HttpAccess *access, const HttpRequest &request,
                 AuthPrincipal *principal) {
    if (access == nullptr || principal == nullptr) {
        return false;
    }
    *principal = access->Authenticate(request);
    return !principal->user_name.empty() && !principal->must_change_password;
}

HttpResponse RequireAuthResponse(HttpAccess *access,
                                 const HttpRequest &request,
                                 AuthPrincipal *principal) {
    if (access == nullptr || principal == nullptr) {
        return StatusResponse(401, "Unauthorized");
    }
    *principal = access->Authenticate(request);
    if (principal->user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    if (principal->must_change_password) {
        return ForbiddenResponse(*principal);
    }
    HttpResponse response;
    response.status_code = 0;
    return response;
}

bool RequirePermissionOrForbidden(HttpAccess *access,
                                  const HttpRequest &request,
                                  AuthPermission permission,
                                  const std::string &target,
                                  AuthPrincipal *principal) {
    return access != nullptr &&
           access->RequirePermission(request, permission, target, principal);
}

bool ParseJsonObject(const HttpRequest &request, ConfigJson *body) {
    if (body == nullptr) {
        return false;
    }
    *body = ConfigJson::parse(request.body, nullptr, false);
    return !body->is_discarded() && body->is_object();
}

bool ParseOptionalJsonObject(const HttpRequest &request, ConfigJson *body) {
    if (body == nullptr) {
        return false;
    }
    if (request.body.empty()) {
        *body = ConfigJson::object();
        return true;
    }
    return ParseJsonObject(request, body);
}

bool IsMediaRestarting(IDeviceMedia *device_media) {
    return device_media != nullptr && device_media->IsRestarting();
}

std::string PathSuffix(const std::string &path, const std::string &prefix) {
    if (!StartsWith(path, prefix)) {
        return std::string();
    }
    return path.substr(prefix.size());
}

std::string
BuildStreamingHeaderBlock(int status_code,
                          const std::map<std::string, std::string> &headers) {
    std::string out = "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n";
    for (const auto &header : headers) {
        out += header.first + ": " + header.second + "\r\n";
    }
    out += "Connection: keep-alive\r\n";
    out += "\r\n";
    return out;
}

bool StartsWith(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

bool IsAiConfigEnabled(IConfig *config) {
    if (config == nullptr) {
        return false;
    }
    ConfigJson ai_config = config->GetValue("ai");
    bool enabled = false;
    return ai_config.is_object() &&
           json_utils::ReadField(ai_config, "enabled", &enabled) &&
           enabled;
}

bool IsAiHealthy(const IAiView *ai) {
    if (ai == nullptr) {
        return false;
    }
    const AiStats stats = ai->GetStats();
    return !stats.enabled || stats.backend_available;
}

const char *StreamIdToJsonString(StreamId stream_id) {
    switch (stream_id) {
        case StreamId::kMain:
            return "main";
        case StreamId::kSub:
            return "sub";
        case StreamId::kSnapshot:
            return "snapshot";
    }
    return "unknown";
}

bool StreamIdFromJsonString(const std::string &value, StreamId *stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    if (value == "main") {
        *stream_id = StreamId::kMain;
        return true;
    }
    if (value == "sub") {
        *stream_id = StreamId::kSub;
        return true;
    }
    return false;
}

}  // namespace live_stream
