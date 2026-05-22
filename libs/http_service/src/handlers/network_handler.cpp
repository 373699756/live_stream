#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "network_service.h"

#include <string>
#include <vector>

namespace live_stream {

namespace {

bool RequireNetworkPermission(HttpAccess *access,
                              const HttpRequest &request,
                              AuthPermission permission,
                              const std::string &target,
                              AuthPrincipal *principal) {
    return RequirePermissionOrForbidden(access, request, permission, target,
                                        principal);
}

}  // namespace

class NetworkHttpHandler : public IHttpHandler {
public:
    NetworkHttpHandler(HttpAccess *access,
                       INetworkService *network_service)
        : access_(access), network_service_(network_service) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/network/interfaces",
                              &NetworkHttpHandler::HandleInterfacesRoute,
                              this);
        router->AddPrefixRoute(HttpMethod::kGet, "/api/network/interfaces/",
                               &NetworkHttpHandler::HandleInterfaceRoute,
                               this);
        router->AddPrefixRoute(HttpMethod::kPut, "/api/network/interfaces/",
                               &NetworkHttpHandler::HandleInterfaceRoute,
                               this);
        router->AddExactRoute(HttpMethod::kPost, "/api/network/reload",
                              &NetworkHttpHandler::HandleReloadRoute, this);
    }

private:
    static HttpResponse HandleInterfacesRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<NetworkHttpHandler *>(user)->HandleInterfaces(
            request);
    }

    static HttpResponse HandleInterfaceRoute(void *user,
                                             const HttpRequest &request) {
        return static_cast<NetworkHttpHandler *>(user)->HandleInterface(
            request);
    }

    static HttpResponse HandleReloadRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<NetworkHttpHandler *>(user)->HandleReload(request);
    }

    HttpResponse HandleInterfaces(const HttpRequest &request) {
        INetworkService *network_service = network_service_;
        if (network_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireNetworkPermission(access_, request,
                                      AuthPermission::kReadStatus, "network",
                                      &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        const std::vector<std::string> ifnames =
            network_service->GetInterfaces();
        for (const std::string &ifname : ifnames) {
            items.push_back(NetworkInterfaceStatusToApiJson(
                network_service->GetInterfaceStatus(ifname)));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleInterface(const HttpRequest &request) {
        INetworkService *network_service = network_service_;
        if (network_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        const std::string ifname =
            PathSuffix(request.path, "/api/network/interfaces/");
        if (ifname.empty()) {
            return StatusResponse(400, "Missing interface");
        }
        if (request.method == HttpMethod::kGet) {
            AuthPrincipal principal;
            if (!RequireNetworkPermission(access_, request,
                                          AuthPermission::kReadStatus, ifname,
                                          &principal)) {
                return StatusResponse(403, "Forbidden");
            }
            const NetworkInterfaceStatus status =
                network_service->GetInterfaceStatus(ifname);
            if (status.ifname.empty()) {
                return StatusResponse(404, "Not Found");
            }
            return JsonResponse(200, NetworkInterfaceStatusToApiJson(status));
        }

        AuthPrincipal principal;
        if (!RequireNetworkPermission(access_, request,
                                      AuthPermission::kModifyConfig, ifname,
                                      &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        NetworkInterfaceConfig config;
        if (!NetworkInterfaceConfigFromApiJson(ifname, body, &config)) {
            return StatusResponse(400, "Invalid network config");
        }
        return network_service
                       ->ApplyInterfaceConfig(access_->MakeContext(request, &principal),
                                              config)
                   ? OkResponse()
                   : StatusResponse(400, "Could not apply network config");
    }

    HttpResponse HandleReload(const HttpRequest &request) {
        INetworkService *network_service = network_service_;
        if (network_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireNetworkPermission(access_, request,
                                      AuthPermission::kModifyConfig, "network",
                                      &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        return network_service->ReloadStatus()
                   ? OkResponse()
                   : StatusResponse(503, "Could not reload network status");
    }

    HttpAccess *access_ = nullptr;
    INetworkService *network_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateNetworkHttpHandler(HttpAccess *access,
                         INetworkService *network_service) {
    return std::unique_ptr<IHttpHandler>(
        new NetworkHttpHandler(access, network_service));
}

}  // namespace live_stream
