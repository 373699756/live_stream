#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "config_service.h"

#include <string>

namespace live_stream {
namespace {

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

}  // namespace

class ConfigHttpHandler : public IHttpHandler {
public:
    ConfigHttpHandler(HttpAccess *access, IConfigService *config_service)
        : access_(access), config_service_(config_service) {}

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
        IConfigService *config_service = config_service_;
        if (config_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        const std::string name = PathSuffix(request.path, "/api/config/");
        if (name.empty()) {
            return StatusResponse(400, "Missing config name");
        }
        if (request.method == HttpMethod::kGet) {
            AuthPrincipal principal;
            if (!RequireAuth(access_, request, &principal)) {
                return StatusResponse(401, "Unauthorized");
            }
            ConfigJson config = config_service->GetValue(name);
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
            if (!RequirePermissionOrForbidden(access_, request,
                                              AuthPermission::kModifyConfig,
                                              name, &principal)) {
                return StatusResponse(403, "Forbidden");
            }
            ConfigJson config;
            if (!ParseJsonObject(request, &config)) {
                return StatusResponse(400, "Invalid JSON");
            }
            if (!config.is_object()) {
                return StatusResponse(400, "Config payload must be an object");
            }
            if (IsWrappedConfigPayload(name, config)) {
                return StatusResponse(
                    400, "Config payload must be the top-level node");
            }
            bool ok = config_service->SetValue(name, config);
            std::string failure_reason;
            if (!ok) {
                failure_reason =
                    FormatConfigError(config_service->GetLastConfigError(name));
            }
            access_->RecordOperation(
                request, principal, OperationAction::kModifyConfig, name,
                ok ? OperationResult::kSuccess : OperationResult::kFailed,
                ok ? std::string() : failure_reason);
            if (!ok) {
                return StatusResponse(400, failure_reason);
            }
            return OkResponse();
        }
        return StatusResponse(404, "Not Found");
    }

    HttpAccess *access_ = nullptr;
    IConfigService *config_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateConfigHttpHandler(HttpAccess *access,
                                                      IConfigService *config_service) {
    return std::unique_ptr<IHttpHandler>(
        new ConfigHttpHandler(access, config_service));
}

}  // namespace live_stream
