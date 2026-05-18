// handlers/network_handler.cpp.inc
// Network handlers: /api/network/*

HttpResponse HandleNetworkInterfaces(const HttpRequest &request) {
    if (dependencies_.network_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReadStatus, "network",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson root = ConfigJson::object();
    ConfigJson items = ConfigJson::array();
    const std::vector<std::string> ifnames =
        dependencies_.network_service->GetInterfaces();
    for (const std::string &ifname : ifnames) {
        items.push_back(NetworkInterfaceStatusToApiJson(
            dependencies_.network_service->GetInterfaceStatus(ifname)));
    }
    root["items"] = items;
    return JsonResponse(200, root);
}

HttpResponse HandleNetworkInterface(const HttpRequest &request) {
    if (dependencies_.network_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    const std::string prefix = "/api/network/interfaces/";
    const std::string ifname = request.path.substr(prefix.size());
    if (ifname.empty()) {
        return StatusResponse(400, "Missing interface");
    }
    if (request.method == HttpMethod::kGet) {
        AuthPrincipal principal;
        if (!RequirePermission(request, AuthPermission::kReadStatus, ifname,
                               &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        const NetworkInterfaceStatus status =
            dependencies_.network_service->GetInterfaceStatus(ifname);
        if (status.ifname.empty()) {
            return StatusResponse(404, "Not Found");
        }
        return JsonResponse(200, NetworkInterfaceStatusToApiJson(status));
    }

    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, ifname,
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return StatusResponse(400, "Invalid JSON");
    }
    NetworkInterfaceConfig config;
    if (!NetworkInterfaceConfigFromApiJson(ifname, body, &config)) {
        return StatusResponse(400, "Invalid network config");
    }
    return dependencies_.network_service->ApplyInterfaceConfig(
               MakeContext(request, &principal), config)
               ? OkResponse()
               : StatusResponse(400, "Could not apply network config");
}

HttpResponse HandleNetworkReload(const HttpRequest &request) {
    if (dependencies_.network_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "network",
                           &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return dependencies_.network_service->ReloadStatus()
               ? OkResponse()
               : StatusResponse(503, "Could not reload network status");
}
