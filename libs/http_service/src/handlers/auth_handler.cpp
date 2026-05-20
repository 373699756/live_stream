#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "live_stream/json_utils.h"

#include <string>

namespace live_stream {
namespace {

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

}  // namespace

HttpResponse http_handlers::HandleLogin(HttpHandlerContext *context, const HttpRequest &request) {
    ConfigJson parsed;
    if (!ParseJsonObject(request, &parsed)) {
        context->IncrementParseFailures();
        return StatusResponse(400, "Invalid JSON");
    }
    std::string user_name;
    std::string password;
    if (!json_utils::Load(parsed, "user_name", &user_name) ||
        !json_utils::Load(parsed, "password", &password)) {
        return StatusResponse(400, "Invalid login request");
    }

    LoginRequest login_request;
    login_request.context = context->MakeContext(request, nullptr);
    login_request.user_name = user_name;
    login_request.password = password;
    LoginResult login = context->Dependencies().auth_service->Login(login_request);
    if (login.token.empty()) {
        context->IncrementAuthFailures();
        return StatusResponse(401, "Unauthorized");
    }

    ConfigJson root = ConfigJson::object();
    root["token"] = login.token;
    root["expires_at_ms"] = login.expires_at_ms;
    root["principal"] = PrincipalToJson(login.principal);
    return JsonResponse(200, root);
}

HttpResponse http_handlers::HandleLogout(HttpHandlerContext *context,
                                         const HttpRequest &request) {
    AuthPrincipal principal;
    if (!RequireAuth(context, request, &principal)) {
        return StatusResponse(401, "Unauthorized");
    }
    live_stream::RequestContext request_context =
        context->MakeContext(request, &principal);
    if (!context->Dependencies().auth_service->Logout(request_context)) {
        return StatusResponse(404, "Not Found");
    }
    return OkResponse();
}

HttpResponse http_handlers::HandleMe(HttpHandlerContext *context, const HttpRequest &request) {
    AuthPrincipal principal;
    if (!RequireAuth(context, request, &principal)) {
        return StatusResponse(401, "Unauthorized");
    }
    return JsonResponse(200, PrincipalToJson(principal));
}

}  // namespace live_stream
