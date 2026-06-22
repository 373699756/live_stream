#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "network_api.h"

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
                       INetwork *network)
        : access_(access), network_(network) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet,
                              "/api/system/network/interfaces",
                              &NetworkHttpHandler::HandleInterfacesRoute,
                              this);
        router->AddPrefixRoute(HttpMethod::kGet,
                               "/api/system/network/interfaces/",
                               &NetworkHttpHandler::HandleInterfaceRoute,
                               this);
        router->AddPrefixRoute(HttpMethod::kPut,
                               "/api/system/network/interfaces/",
                               &NetworkHttpHandler::HandleInterfaceRoute,
                               this);
        router->AddExactRoute(HttpMethod::kPost,
                              "/api/system/network/reload",
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
        INetwork *network = network_;
        if (network == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireNetworkPermission(access_, request,
                                      AuthPermission::kReadStatus, "network",
                                      &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        const std::vector<std::string> ifnames =
            network->GetInterfaces();
        for (const std::string &ifname : ifnames) {
            items.push_back(NetStatusToApiJson(
                network->GetInterfaceStatus(ifname)));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleInterface(const HttpRequest &request) {
        INetwork *network = network_;
        if (network == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        const std::string ifname =
            PathSuffix(request.path, "/api/system/network/interfaces/");
        if (ifname.empty()) {
            return StatusResponse(400, "Missing interface");
        }
        if (request.method == HttpMethod::kGet) {
            AuthPrincipal principal;
            if (!RequireNetworkPermission(access_, request,
                                          AuthPermission::kReadStatus, ifname,
                                          &principal)) {
                return ForbiddenResponse(principal);
            }
            const NetStatus status =
                network->GetInterfaceStatus(ifname);
            if (status.ifname.empty()) {
                return StatusResponse(404, "Not Found");
            }
            return JsonResponse(200, NetStatusToApiJson(status));
        }

        AuthPrincipal principal;
        if (!RequireNetworkPermission(access_, request,
                                      AuthPermission::kModifyConfig, ifname,
                                      &principal)) {
            return ForbiddenResponse(principal);
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        NetConfig config;
        if (!NetConfigFromApiJson(ifname, body, &config)) {
            return StatusResponse(400, "Invalid network config");
        }
        return network
                       ->ApplyInterfaceConfig(access_->MakeContext(request, &principal),
                                              config)
                   ? OkResponse()
                   : StatusResponse(400, "Could not apply network config");
    }

    HttpResponse HandleReload(const HttpRequest &request) {
        INetwork *network = network_;
        if (network == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireNetworkPermission(access_, request,
                                      AuthPermission::kModifyConfig, "network",
                                      &principal)) {
            return ForbiddenResponse(principal);
        }
        return network->ReloadStatus()
                   ? OkResponse()
                   : StatusResponse(503, "Could not reload network status");
    }

    HttpAccess *access_ = nullptr;
    INetwork *network_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeNetworkHandler(HttpAccess *access,
                                                 INetwork *network) {
    return std::unique_ptr<IHttpHandler>(
        new NetworkHttpHandler(access, network));
}

}  // namespace live_stream
