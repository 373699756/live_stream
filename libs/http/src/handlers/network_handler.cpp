#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_json_body.h"
#include "http_path.h"
#include "http_response.h"

#include "system/network_json.h"

#include <string>
#include <vector>

namespace live_stream {

class NetworkHttpHandler : public IHttpHandler {
public:
    explicit NetworkHttpHandler(
        const NetworkHandlerRefs &refs)
        : access_(refs.access), network_(refs.network) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (network_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kGet,
                             "/api/system/network/interfaces", this,
                             &NetworkHttpHandler::HandleInterfaces);
        router.AddPrefixRoute(HttpMethod::kGet,
                              "/api/system/network/interfaces/", this,
                              &NetworkHttpHandler::HandleInterface);
        router.AddPrefixRoute(HttpMethod::kPut,
                              "/api/system/network/interfaces/", this,
                              &NetworkHttpHandler::HandleInterface);
        router.AddExactRoute(HttpMethod::kPost,
                             "/api/system/network/reload", this,
                             &NetworkHttpHandler::HandleReload);
    }

private:
    HttpResponse HandleInterfaces(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "network",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json root = Json::object();
        Json items = Json::array();
        const std::vector<std::string> ifnames =
            network_->GetInterfaces();
        for (const std::string &ifname : ifnames) {
            items.push_back(NetInterfaceInfoToApiJson(
                network_->GetInterfaceInfo(ifname)));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleInterface(const HttpRequest &request) {
        const std::string ifname =
            PathSuffix(request.path, "/api/system/network/interfaces/");
        if (ifname.empty()) {
            return StatusResponse(400, "Missing interface");
        }
        if (request.method == HttpMethod::kGet) {
            AuthPrincipal principal;
            HttpResponse auth_response = RequirePermissionResponse(
                access_, request, AuthPermission::kReadStatus, ifname,
                &principal);
            if (auth_response.status_code != 0) {
                return auth_response;
            }
            const NetInterfaceInfo interface_info =
                network_->GetInterfaceInfo(ifname);
            if (interface_info.ifname.empty()) {
                return StatusResponse(404, "Not Found");
            }
            return JsonResponse(200,
                                NetInterfaceInfoToApiJson(interface_info));
        }

        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, ifname,
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        NetConfig config;
        if (!NetConfigFromApiJson(ifname, body, &config)) {
            return StatusResponse(400, "Invalid network config");
        }
        return network_->ApplyInterfaceConfig(
                   access_->MakeContext(request, &principal), config)
                   ? OkResponse()
                   : StatusResponse(400, "Could not apply network config");
    }

    HttpResponse HandleReload(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kModifyConfig, "network",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return network_->ReloadInterfaceInfo()
                   ? OkResponse()
                   : StatusResponse(503, "Could not reload network status");
    }

    HttpAccess *access_ = nullptr;
    INetwork *network_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeNetworkHandler(
    const NetworkHandlerRefs &refs) {
    return std::unique_ptr<IHttpHandler>(
        new NetworkHttpHandler(refs));
}

}  // namespace live_stream
