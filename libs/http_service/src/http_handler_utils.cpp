#include "http_handler_utils.h"

#include "ai_service.h"
#include "config_service.h"
#include "live_stream/json_utils.h"
#include "media_service.h"

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

HttpResponse OkResponse() {
    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    return JsonResponse(200, root);
}

bool RequireAuth(HttpHandlerContext *context, const HttpRequest &request,
                 AuthPrincipal *principal) {
    if (context == nullptr || principal == nullptr) {
        return false;
    }
    *principal = context->Authenticate(request);
    return !principal->user_name.empty();
}

bool RequirePermissionOrForbidden(HttpHandlerContext *context,
                                  const HttpRequest &request,
                                  AuthPermission permission,
                                  const std::string &target,
                                  AuthPrincipal *principal) {
    return context != nullptr &&
           context->RequirePermission(request, permission, target, principal);
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

bool IsMediaRestarting(HttpHandlerContext *context) {
    return context != nullptr &&
           context->Dependencies().media_service != nullptr &&
           context->Dependencies().media_service->IsRestarting();
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
    out += "Connection: close\r\n";
    out += "\r\n";
    return out;
}

bool StartsWith(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

bool IsAiConfigEnabled(IConfigService *config_service) {
    if (config_service == nullptr) {
        return false;
    }
    ConfigJson config = config_service->GetValue("ai");
    bool enabled = false;
    return config.is_object() && json_utils::Load(config, "enabled", &enabled) &&
           enabled;
}

bool IsAiServiceHealthy(const IAiView *service) {
    if (service == nullptr) {
        return false;
    }
    const AiServiceStats stats = service->GetStats();
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
