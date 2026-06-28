#include "http_media_response.h"

namespace live_stream {

HttpResponse HttpMediaJsonResponse(int status_code,
                                   const Json &value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse HttpMediaStatusResponse(int status_code,
                                     const std::string &msg) {
    Json root = Json::object();
    Json error = Json::object();
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
    error["message"] = msg;
    root["error"] = error;
    return HttpMediaJsonResponse(status_code, root);
}

HttpResponse HttpMediaTextResponse(int status_code, const std::string &msg) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "text/plain";
    response.body = msg;
    return response;
}

HttpResponse HttpMediaForbiddenResponse(const AuthPrincipal &principal) {
    if (principal.must_change_password) {
        return HttpMediaStatusResponse(403, "must_change_password");
    }
    return HttpMediaStatusResponse(403, "Forbidden");
}

HttpResponse HttpMediaOkResponse() {
    return HttpMediaJsonResponse(200, Json::object());
}

std::string BuildHttpMediaStreamHeader(
    int status_code, const std::map<std::string, std::string> &headers) {
    std::string header_block =
        "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n";
    for (const auto &header : headers) {
        header_block += header.first + ": " + header.second + "\r\n";
    }
    header_block += "Connection: keep-alive\r\n";
    header_block += "\r\n";
    return header_block;
}

}  // namespace live_stream
