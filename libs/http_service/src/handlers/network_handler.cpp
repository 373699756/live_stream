#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "network_service.h"

#include <string>
#include <vector>

namespace live_stream {

namespace {

INetworkService *RequireNetworkService(HttpHandlerContext *context) {
    return context->Dependencies().network_service;
}

bool RequireNetworkPermission(HttpHandlerContext *context,
                              const HttpRequest &request,
                              AuthPermission permission,
                              const std::string &target,
                              AuthPrincipal *principal) {
    return RequirePermissionOrForbidden(context, request, permission, target,
                                        principal);
}

}  // namespace

HttpResponse http_handlers::HandleNetworkInterfaces(HttpHandlerContext *context, const HttpRequest &request) {
    INetworkService *network_service = RequireNetworkService(context);
    if (network_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireNetworkPermission(context, request, AuthPermission::kReadStatus,
                                  "network", &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson root = ConfigJson::object();
    ConfigJson items = ConfigJson::array();
    const std::vector<std::string> ifnames = network_service->GetInterfaces();
    for (const std::string &ifname : ifnames) {
        items.push_back(
            NetworkInterfaceStatusToApiJson(
                network_service->GetInterfaceStatus(ifname)));
    }
    root["items"] = items;
    return JsonResponse(200, root);
}

HttpResponse http_handlers::HandleNetworkInterface(HttpHandlerContext *context, const HttpRequest &request) {
    INetworkService *network_service = RequireNetworkService(context);
    if (network_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    const std::string ifname = PathSuffix(request.path, "/api/network/interfaces/");
    if (ifname.empty()) {
        return StatusResponse(400, "Missing interface");
    }
    if (request.method == HttpMethod::kGet) {
        AuthPrincipal principal;
        if (!RequireNetworkPermission(context, request,
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
    if (!RequireNetworkPermission(context, request,
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
    return network_service->ApplyInterfaceConfig(
               context->MakeContext(request, &principal), config)
               ? OkResponse()
               : StatusResponse(400, "Could not apply network config");
}

HttpResponse http_handlers::HandleNetworkReload(HttpHandlerContext *context, const HttpRequest &request) {
    INetworkService *network_service = RequireNetworkService(context);
    if (network_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequireNetworkPermission(context, request,
                                  AuthPermission::kModifyConfig, "network",
                                  &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return network_service->ReloadStatus()
               ? OkResponse()
               : StatusResponse(503, "Could not reload network status");
}

}  // namespace live_stream
