#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_response.h"
#include "handlers/system_overview_response.h"

#include "system.h"

#include <string>

namespace live_stream {
namespace {

Json SystemCapabilitiesToJson(const SystemCapabilities &capabilities) {
    Json root = Json::object();
    root["supports_reboot"] = capabilities.supports_reboot;
    root["supports_factory_reset"] = capabilities.supports_factory_reset;
    Json features = Json::array();
    for (const std::string &feature : capabilities.features) {
        features.push_back(feature);
    }
    root["features"] = features;
    return root;
}

}  // namespace

class SystemHttpHandler : public IHttpHandler {
public:
    explicit SystemHttpHandler(const SystemHandlerDependencies &dependencies)
        : access_(dependencies.access),
          system_(dependencies.system),
          overview_(dependencies.overview) {}

    void RegisterRoutes(IHttpRouter &router) override {
        router.AddExactRoute(HttpMethod::kGet, "/api/system/status",
                             this, &SystemHttpHandler::HandleOverview);
        if (system_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kGet, "/api/system/capabilities",
                             this, &SystemHttpHandler::HandleCapabilities);
        router.AddExactRoute(HttpMethod::kPost, "/api/system/reboot",
                             this, &SystemHttpHandler::HandleReboot);
        router.AddExactRoute(HttpMethod::kPost,
                             "/api/system/factory-reset", this,
                             &SystemHttpHandler::HandleFactoryReset);
    }

private:
    HttpResponse HandleOverview(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }

        return JsonResponse(200,
                            BuildSystemOverviewJson(system_, overview_));
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "system",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return JsonResponse(
            200, SystemCapabilitiesToJson(
                     system_->GetCapabilities()));
    }

    HttpResponse HandleReboot(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReboot, "system", &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        const SystemCapabilities capabilities =
            system_->GetCapabilities();
        if (!capabilities.supports_reboot) {
            return StatusResponse(501, "Reboot not supported");
        }
        return system_->Reboot(
                   access_->MakeContext(request, &principal))
                   ? OkResponse()
                   : StatusResponse(503, "Reboot failed");
    }

    HttpResponse HandleFactoryReset(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kFactoryReset, "system",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        const SystemCapabilities capabilities =
            system_->GetCapabilities();
        if (!capabilities.supports_factory_reset) {
            return StatusResponse(501, "Factory reset not supported");
        }
        return system_->FactoryReset(
                   access_->MakeContext(request, &principal))
                   ? OkResponse()
                   : StatusResponse(503, "Factory reset failed");
    }

    HttpAccess *access_ = nullptr;
    ISystem *system_ = nullptr;
    SystemOverviewSources overview_;
};

std::unique_ptr<IHttpHandler> MakeSystemHandler(
    const SystemHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new SystemHttpHandler(dependencies));
}

}  // namespace live_stream
