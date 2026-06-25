#include "http_response.h"

#include "http_path.h"
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

bool IsEnvelopeObject(const Json &value) {
    return value.is_object() && value.contains("ok") &&
           value.contains("data") && value.contains("error") &&
           value.contains("request_id");
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

HttpResponse JsonEnvelopeResponse(int status_code, const Json &data,
                                  const std::string &request_id) {
    Json root = Json::object();
    root["ok"] = status_code >= 200 && status_code < 300;
    root["data"] = data;
    root["error"] = nullptr;
    root["request_id"] = request_id;
    return JsonResponse(status_code, root);
}

HttpResponse ErrorEnvelopeResponse(int status_code, HttpErrorCode code,
                                   const std::string &msg,
                                   const std::string &request_id) {
    Json root = Json::object();
    Json error = Json::object();
    error["code"] = HttpErrorCodeName(code);
    error["message"] = msg;
    root["ok"] = false;
    root["data"] = nullptr;
    root["error"] = error;
    root["request_id"] = request_id;
    return JsonResponse(status_code, root);
}

}  // namespace

HttpResponse JsonResponse(int status_code, const Json &value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse ErrorResponse(int status_code, HttpErrorCode code,
                           const std::string &msg) {
    Json root = Json::object();
    Json error = Json::object();
    error["code"] = HttpErrorCodeName(code);
    error["message"] = msg;
    root["error"] = error;
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
    return ErrorResponse(403, HttpErrorCode::kPermissionDenied, "Forbidden");
}

HttpResponse OkResponse() {
    return JsonResponse(200, Json::object());
}

HttpResponse AddJsonEnvelope(const HttpRequest &request,
                             const HttpResponse &response) {
    if (!StartsWith(request.path, "/api/") || !IsJsonContentType(response)) {
        return response;
    }

    Json body = Json::parse(response.body, nullptr, false);
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
        std::string msg = "Request failed";
        if (body.is_object()) {
            const Json::const_iterator error_iter = body.find("error");
            if (error_iter != body.end()) {
                if (error_iter->is_string()) {
                    msg = error_iter->get<std::string>();
                } else if (error_iter->is_object()) {
                    const Json::const_iterator code_iter =
                        error_iter->find("code");
                    const Json::const_iterator message_iter =
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
                        msg = message_iter->get<std::string>();
                    }
                }
            }
        }
        wrapped = ErrorEnvelopeResponse(response.status_code, code, msg,
                                        RequestIdForResponse(request));
    }
    for (const auto &header : response.headers) {
        wrapped.headers[header.first] = header.second;
    }
    wrapped.headers["Content-Type"] = "application/json";
    return wrapped;
}

}  // namespace live_stream
