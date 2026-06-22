#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "config.h"

#include <string>

namespace live_stream {
namespace {

const char *kSetConfigFailed = "set config failed";
const char *kUnsupportedConfigScopeAudio = "audio";

bool IsWrappedConfigPayload(const std::string &name, const ConfigJson &value) {
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

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddPrefixRoute(HttpMethod::kGet, "/api/config/",
                               &ConfigHttpHandler::HandleConfigRoute, this);
        router->AddPrefixRoute(HttpMethod::kPut, "/api/config/",
                               &ConfigHttpHandler::HandleConfigRoute, this);
    }

private:
    static HttpResponse HandleConfigRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<ConfigHttpHandler *>(user)->HandleConfig(request);
    }

    HttpResponse HandleConfig(const HttpRequest &request) {
        IConfig *config = config_;
        if (config == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
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
            ConfigJson value = config->Get(name);
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
            if (!RequirePermissionOrForbidden(access_, request,
                                              AuthPermission::kModifyConfig,
                                              name, &principal)) {
                return ForbiddenResponse(principal);
            }
            if (IsUnsupportedConfigScope(name)) {
                return StatusResponse(404, "Not Found");
            }
            ConfigJson value;
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
            ConfigIssue issue;
            const ConfigStatus status = config->Set(name, value, &issue);
            const bool ok = status == ConfigStatus::kOk;
            const std::string failure_reason =
                ok ? std::string()
                   : (issue.reason.empty() ? std::string(kSetConfigFailed)
                                           : issue.reason);
            access_->RecordOperation(
                request, principal, OperationAction::kModifyConfig, name,
                ok ? OperationResult::kSuccess : OperationResult::kFailed,
                failure_reason);
            if (!ok) {
                return StatusResponse(400, failure_reason);
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
