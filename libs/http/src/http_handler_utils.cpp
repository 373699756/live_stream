#include "http_handler_utils.h"

#include "ai.h"
#include "config.h"
#include "json_utils.h"
#include "device.h"
#include "http_protocol.h"

namespace live_stream {

namespace {

bool IsJsonContentType(const HttpResponse &response) {
    const auto iter = response.headers.find("Content-Type");
    if (iter == response.headers.end()) {
        return false;
    }
    return ToLower(iter->second).find("application/json") !=
           std::string::npos;
}

bool IsEnvelopeObject(const ConfigJson &value) {
    return value.is_object() && value.contains("ok") &&
           value.contains("data") && value.contains("error") &&
           value.contains("request_id");
}

}  // namespace

HttpResponse JsonResponse(int status_code, const ConfigJson &value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse JsonEnvelopeResponse(int status_code, const ConfigJson &data,
                                  const std::string &request_id) {
    ConfigJson root = ConfigJson::object();
    root["ok"] = status_code >= 200 && status_code < 300;
    root["data"] = data;
    root["error"] = nullptr;
    root["request_id"] = request_id;
    return JsonResponse(status_code, root);
}

HttpResponse ErrorResponse(int status_code, HttpErrorCode code,
                           const std::string &message) {
    ConfigJson root = ConfigJson::object();
    ConfigJson error = ConfigJson::object();
    error["code"] = HttpErrorCodeName(code);
    error["message"] = message;
    root["error"] = error;
    return JsonResponse(status_code, root);
}

HttpResponse ErrorEnvelopeResponse(int status_code, HttpErrorCode code,
                                   const std::string &message,
                                   const std::string &request_id) {
    ConfigJson root = ConfigJson::object();
    ConfigJson error = ConfigJson::object();
    error["code"] = HttpErrorCodeName(code);
    error["message"] = message;
    root["ok"] = false;
    root["data"] = nullptr;
    root["error"] = error;
    root["request_id"] = request_id;
    return JsonResponse(status_code, root);
}

HttpResponse StatusResponse(int status_code, const std::string &reason) {
    return ErrorResponse(status_code, HttpErrorCodeForStatus(status_code),
                         reason);
}

HttpResponse ForbiddenResponse(const AuthPrincipal &principal) {
    if (principal.must_change_password) {
        return ErrorResponse(403, HttpErrorCode::kPermissionDenied,
                             "must_change_password");
    }
    return ErrorResponse(403, HttpErrorCode::kPermissionDenied,
                         "Forbidden");
}

HttpResponse OkResponse() {
    return JsonResponse(200, ConfigJson::object());
}

HttpResponse AddJsonEnvelope(const HttpRequest &request,
                             const HttpResponse &response) {
    if (!StartsWith(request.path, "/api/") || !IsJsonContentType(response)) {
        return response;
    }

    ConfigJson body = ConfigJson::parse(response.body, nullptr, false);
    if (body.is_discarded()) {
        return ErrorEnvelopeResponse(500, HttpErrorCode::kInternalError,
                                     "Invalid JSON response",
                                     RequestIdForResponse(request));
    }
    if (IsEnvelopeObject(body)) {
        return response;
    }
    HttpResponse wrapped;
    if (response.status_code >= 200 && response.status_code < 300) {
        wrapped = JsonEnvelopeResponse(response.status_code, body,
                                       RequestIdForResponse(request));
    } else {
        HttpErrorCode code = HttpErrorCodeForStatus(response.status_code);
        std::string message = "Request failed";
        if (body.is_object()) {
            const ConfigJson::const_iterator error_iter = body.find("error");
            if (error_iter != body.end()) {
                if (error_iter->is_string()) {
                    message = error_iter->get<std::string>();
                } else if (error_iter->is_object()) {
                    const ConfigJson::const_iterator code_iter =
                        error_iter->find("code");
                    const ConfigJson::const_iterator message_iter =
                        error_iter->find("message");
                    if (code_iter != error_iter->end() &&
                        code_iter->is_string()) {
                        const std::string code_text =
                            code_iter->get<std::string>();
                        if (code_text == "invalid_argument") {
                            code = HttpErrorCode::kInvalidArgument;
                        } else if (code_text == "unauthenticated") {
                            code = HttpErrorCode::kUnauthenticated;
                        } else if (code_text == "permission_denied") {
                            code = HttpErrorCode::kPermissionDenied;
                        } else if (code_text == "stream_not_found") {
                            code = HttpErrorCode::kStreamNotFound;
                        } else if (code_text == "protocol_unavailable") {
                            code = HttpErrorCode::kProtocolUnavailable;
                        } else if (code_text == "peer_not_found") {
                            code = HttpErrorCode::kPeerNotFound;
                        } else if (code_text == "resource_busy") {
                            code = HttpErrorCode::kResourceBusy;
                        } else if (code_text == "internal_error") {
                            code = HttpErrorCode::kInternalError;
                        }
                    }
                    if (message_iter != error_iter->end() &&
                        message_iter->is_string()) {
                        message = message_iter->get<std::string>();
                    }
                }
            }
        }
        wrapped = ErrorEnvelopeResponse(response.status_code, code, message,
                                        RequestIdForResponse(request));
    }
    for (const auto &header : response.headers) {
        wrapped.headers[header.first] = header.second;
    }
    wrapped.headers["Content-Type"] = "application/json";
    return wrapped;
}

std::string RequestIdForResponse(const HttpRequest &request) {
    return request.request_id.empty() ? std::string("http-0")
                                      : request.request_id;
}

const char *HttpErrorCodeName(HttpErrorCode code) {
    switch (code) {
        case HttpErrorCode::kInvalidArgument:
            return "invalid_argument";
        case HttpErrorCode::kUnauthenticated:
            return "unauthenticated";
        case HttpErrorCode::kPermissionDenied:
            return "permission_denied";
        case HttpErrorCode::kStreamNotFound:
            return "stream_not_found";
        case HttpErrorCode::kProtocolUnavailable:
            return "protocol_unavailable";
        case HttpErrorCode::kPeerNotFound:
            return "peer_not_found";
        case HttpErrorCode::kResourceBusy:
            return "resource_busy";
        case HttpErrorCode::kInternalError:
            return "internal_error";
    }
    return "internal_error";
}

HttpErrorCode HttpErrorCodeForStatus(int status_code) {
    if (status_code == 401) {
        return HttpErrorCode::kUnauthenticated;
    }
    if (status_code == 403) {
        return HttpErrorCode::kPermissionDenied;
    }
    if (status_code == 404) {
        return HttpErrorCode::kInvalidArgument;
    }
    if (status_code == 409) {
        return HttpErrorCode::kResourceBusy;
    }
    if (status_code == 501 || status_code == 503) {
        return HttpErrorCode::kProtocolUnavailable;
    }
    if (status_code >= 400 && status_code < 500) {
        return HttpErrorCode::kInvalidArgument;
    }
    return HttpErrorCode::kInternalError;
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
        return ErrorResponse(401, HttpErrorCode::kUnauthenticated,
                             "Unauthorized");
    }
    *principal = access->Authenticate(request);
    if (principal->user_name.empty()) {
        return ErrorResponse(401, HttpErrorCode::kUnauthenticated,
                             "Unauthorized");
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

bool IsMediaRestarting(DeviceMedia *device) {
    return device != nullptr && device->IsRestarting();
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
    ConfigJson ai_config = config->Get("ai");
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
