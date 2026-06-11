#include "http_media_utils.h"

#include "device.h"

namespace live_stream {

HttpResponse HttpMediaJsonResponse(int status_code,
                                   const ConfigJson &value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse HttpMediaStatusResponse(int status_code,
                                     const std::string &reason) {
    ConfigJson root = ConfigJson::object();
    ConfigJson error = ConfigJson::object();
    if (status_code == 401) {
        error["code"] = "unauthenticated";
    } else if (status_code == 403) {
        error["code"] = "permission_denied";
    } else if (status_code == 409) {
        error["code"] = "resource_busy";
    } else if (status_code == 501 || status_code == 503) {
        error["code"] = "protocol_unavailable";
    } else if (status_code >= 400 && status_code < 500) {
        error["code"] = "invalid_argument";
    } else {
        error["code"] = "internal_error";
    }
    error["message"] = reason;
    root["error"] = error;
    return HttpMediaJsonResponse(status_code, root);
}

HttpResponse HttpMediaTextResponse(int status_code,
                                   const std::string &reason) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "text/plain";
    response.body = reason;
    return response;
}

HttpResponse HttpMediaForbiddenResponse(const AuthPrincipal &principal) {
    if (principal.must_change_password) {
        return HttpMediaStatusResponse(403, "must_change_password");
    }
    return HttpMediaStatusResponse(403, "Forbidden");
}

HttpResponse HttpMediaOkResponse() {
    return HttpMediaJsonResponse(200, ConfigJson::object());
}

HttpResponse RequireHttpMediaAuthResponse(HttpAccess *access,
                                          const HttpRequest &request,
                                          AuthPrincipal *principal) {
    if (access == nullptr || principal == nullptr) {
        return HttpMediaStatusResponse(401, "Unauthorized");
    }
    *principal = access->Authenticate(request);
    if (principal->user_name.empty()) {
        return HttpMediaStatusResponse(401, "Unauthorized");
    }
    if (principal->must_change_password) {
        return HttpMediaForbiddenResponse(*principal);
    }
    HttpResponse response;
    response.status_code = 0;
    return response;
}

HttpResponse RequirePlaybackAuthResponse(
    HttpAccess *access, const HttpRequest &request,
    AuthPrincipal *principal) {
    HttpResponse response =
        RequireHttpMediaAuthResponse(access, request, principal);
    if (response.status_code == 0) {
        return response;
    }
    if (response.status_code == 403 &&
        principal != nullptr && principal->must_change_password) {
        return HttpMediaTextResponse(403, "must_change_password");
    }
    if (response.status_code == 401) {
        return HttpMediaTextResponse(401, "Unauthorized");
    }
    return HttpMediaTextResponse(response.status_code, "Forbidden");
}

bool ParseOptionalJsonBody(const HttpRequest &request, ConfigJson *body) {
    if (body == nullptr) {
        return false;
    }
    if (request.body.empty()) {
        *body = ConfigJson::object();
        return true;
    }
    *body = ConfigJson::parse(request.body, nullptr, false);
    return !body->is_discarded() && body->is_object();
}

bool IsHttpMediaRestarting(DeviceMedia *device) {
    return device != nullptr && device->IsRestarting();
}

std::string HttpMediaPathSuffix(const std::string &path,
                                const std::string &prefix) {
    if (!HttpMediaStartsWith(path, prefix)) {
        return std::string();
    }
    return path.substr(prefix.size());
}

std::string BuildHttpStreamHeaderBlock(
    int status_code, const std::map<std::string, std::string> &headers) {
    std::string out = "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n";
    for (const auto &header : headers) {
        out += header.first + ": " + header.second + "\r\n";
    }
    out += "Connection: keep-alive\r\n";
    out += "\r\n";
    return out;
}

bool HttpMediaStartsWith(const std::string &value,
                         const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

const char *MediaStreamIdToJson(StreamId stream_id) {
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

bool MediaStreamIdFromJson(const std::string &value, StreamId *stream_id) {
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
