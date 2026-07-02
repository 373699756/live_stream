#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_json_body.h"
#include "http_path.h"
#include "http_response.h"

#include "config.h"

#include <string>

namespace live_stream {
namespace {

const char *kSetConfigFailed = "set config failed";
const char *kUnsupportedConfigScopeAudio = "audio";

int HttpStatusForConfigCode(ConfigCode code) {
    switch (code) {
        case ConfigCode::kOk:
            return 200;
        case ConfigCode::kInvalid:
        case ConfigCode::kVerify:
            return 400;
        case ConfigCode::kMissing:
            return 404;
        case ConfigCode::kExists:
            return 409;
        case ConfigCode::kStopped:
        case ConfigCode::kApply:
            return 503;
        case ConfigCode::kSave:
            return 500;
    }
    return 500;
}

const char *HttpErrorCodeForConfigCode(ConfigCode code) {
    switch (code) {
        case ConfigCode::kOk:
            return "internal_error";
        case ConfigCode::kInvalid:
        case ConfigCode::kMissing:
        case ConfigCode::kVerify:
            return "invalid_argument";
        case ConfigCode::kExists:
            return "resource_busy";
        case ConfigCode::kStopped:
        case ConfigCode::kApply:
            return "protocol_unavailable";
        case ConfigCode::kSave:
            return "internal_error";
    }
    return "internal_error";
}

std::string RequestIdForConfigResponse(const HttpRequest &request) {
    return request.request_id.empty() ? std::string("http-0")
                                      : request.request_id;
}

HttpResponse ConfigErrorResponse(const HttpRequest &request, ConfigCode code,
                                 const ConfigError &config_error) {
    Json root = Json::object();
    Json error = Json::object();
    error["code"] = HttpErrorCodeForConfigCode(code);
    error["message"] = config_error.message.empty()
                           ? std::string(kSetConfigFailed)
                           : config_error.message;
    if (!config_error.scope.empty()) {
        error["scope"] = config_error.scope;
    }
    if (!config_error.field.empty()) {
        error["field"] = config_error.field;
    }
    root["ok"] = false;
    root["data"] = nullptr;
    root["error"] = error;
    root["request_id"] = RequestIdForConfigResponse(request);
    return JsonResponse(HttpStatusForConfigCode(code), root);
}

bool IsWrappedConfigPayload(const std::string &name, const Json &value) {
    return value.is_object() && value.size() == 1 && value.contains(name) &&
           value.at(name).is_object();
}

bool IsUnsupportedConfigScope(const std::string &name) {
    return name == kUnsupportedConfigScopeAudio;
}

}  // namespace

class ConfigHttpHandler : public IHttpHandler {
public:
    ConfigHttpHandler(HttpAccess *access, IConfig *config)
        : access_(access), config_(config) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (config_ == nullptr) {
            return;
        }
        router.AddPrefixRoute(HttpMethod::kGet, "/api/config/", this,
                              &ConfigHttpHandler::HandleConfig);
        router.AddPrefixRoute(HttpMethod::kPut, "/api/config/", this,
                              &ConfigHttpHandler::HandleConfig);
    }

private:
    HttpResponse HandleConfig(const HttpRequest &request) {
        const std::string name = PathSuffix(request.path, "/api/config/");
        if (name.empty()) {
            return StatusResponse(400, "Missing config name");
        }
        if (request.method == HttpMethod::kGet) {
            AuthPrincipal principal;
            HttpResponse auth_response =
                RequireAuthResponse(access_, request, &principal);
            if (auth_response.status_code != 0) {
                return auth_response;
            }
            if (IsUnsupportedConfigScope(name)) {
                return StatusResponse(404, "Not Found");
            }
            Json value = config_->Get(name);
            if (value.is_null()) {
                return StatusResponse(404, "Not Found");
            }
            HttpResponse response;
            response.status_code = 200;
            response.headers["Content-Type"] = "application/json";
            response.body = value.dump();
            return response;
        }
        if (request.method == HttpMethod::kPut) {
            AuthPrincipal principal;
            HttpResponse auth_response = RequirePermissionResponse(
                access_, request, AuthPermission::kModifyConfig, name,
                &principal);
            if (auth_response.status_code != 0) {
                return auth_response;
            }
            if (IsUnsupportedConfigScope(name)) {
                return StatusResponse(404, "Not Found");
            }
            Json value;
            if (!ParseJsonObject(request, &value)) {
                return StatusResponse(400, "Invalid JSON");
            }
            if (!value.is_object()) {
                return StatusResponse(400, "Config payload must be an object");
            }
            if (IsWrappedConfigPayload(name, value)) {
                return StatusResponse(
                    400, "Config payload must be the top-level node");
            }
            ConfigError config_error;
            const ConfigCode code = config_->Set(name, value, &config_error);
            const bool ok = code == ConfigCode::kOk;
            const std::string failure_reason =
                ok ? std::string()
                   : (config_error.message.empty()
                          ? std::string(kSetConfigFailed)
                          : config_error.message);
            access_->RecordOperation(
                request, principal, OperationAction::kModifyConfig, name,
                ok ? OperationResult::kSuccess : OperationResult::kFailed,
                failure_reason);
            if (!ok) {
                return ConfigErrorResponse(request, code, config_error);
            }
            return OkResponse();
        }
        return StatusResponse(404, "Not Found");
    }

    HttpAccess *access_ = nullptr;
    IConfig *config_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeConfigHandler(HttpAccess *access,
                                                IConfig *config) {
    return std::unique_ptr<IHttpHandler>(
        new ConfigHttpHandler(access, config));
}

}  // namespace live_stream
