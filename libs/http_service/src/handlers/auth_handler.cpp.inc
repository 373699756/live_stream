// handlers/auth_handler.cpp.inc
// Auth handlers: /api/auth/*

std::string AuthRoleToJsonString(AuthRole role) {
    return AuthRoleToString(role);
}

ConfigJson PrincipalToJson(const AuthPrincipal &principal) {
    ConfigJson root = ConfigJson::object();
    root["user_name"] = principal.user_name;
    root["session_id"] = principal.session_id;
    root["role"] = AuthRoleToJsonString(principal.role);
    return root;
}

HttpResponse HandleLogin(const HttpRequest &request) {
    ConfigJson parsed = ConfigJson::parse(request.body, nullptr, false);
    if (parsed.is_discarded()) {
        IncrementParseFailures();
        return StatusResponse(400, "Invalid JSON");
    }
    std::string user_name;
    std::string password;
    if (!json_utils::Load(parsed, "user_name", &user_name) ||
        !json_utils::Load(parsed, "password", &password)) {
        return StatusResponse(400, "Invalid login request");
    }

    LoginRequest login_request;
    login_request.context = MakeContext(request, nullptr);
    login_request.user_name = user_name;
    login_request.password = password;
    LoginResult login = dependencies_.auth_service->Login(login_request);
    if (login.token.empty()) {
        IncrementAuthFailures();
        return StatusResponse(401, "Unauthorized");
    }

    ConfigJson root = ConfigJson::object();
    root["token"] = login.token;
    root["expires_at_ms"] = login.expires_at_ms;
    root["principal"] = PrincipalToJson(login.principal);
    return JsonResponse(200, root);
}

HttpResponse HandleLogout(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    live_stream::RequestContext context = MakeContext(request, &principal);
    if (!dependencies_.auth_service->Logout(context)) {
        return StatusResponse(404, "Not Found");
    }
    return OkResponse();
}

HttpResponse HandleMe(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }
    return JsonResponse(200, PrincipalToJson(principal));
}
