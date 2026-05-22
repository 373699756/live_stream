#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "json_utils.h"

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

class AuthHttpHandler : public IHttpHandler {
public:
    AuthHttpHandler(HttpAccess *access, IAuthService *auth_service)
        : access_(access), auth_service_(auth_service) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kPost, "/api/auth/login",
                              &AuthHttpHandler::HandleLoginRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/auth/logout",
                              &AuthHttpHandler::HandleLogoutRoute, this);
        router->AddExactRoute(HttpMethod::kGet, "/api/auth/me",
                              &AuthHttpHandler::HandleMeRoute, this);
    }

private:
    static HttpResponse HandleLoginRoute(void *user,
                                         const HttpRequest &request) {
        return static_cast<AuthHttpHandler *>(user)->HandleLogin(request);
    }

    static HttpResponse HandleLogoutRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<AuthHttpHandler *>(user)->HandleLogout(request);
    }

    static HttpResponse HandleMeRoute(void *user, const HttpRequest &request) {
        return static_cast<AuthHttpHandler *>(user)->HandleMe(request);
    }

    HttpResponse HandleLogin(const HttpRequest &request) {
        ConfigJson parsed;
        if (!ParseJsonObject(request, &parsed)) {
            access_->IncrementParseFailures();
            return StatusResponse(400, "Invalid JSON");
        }
        std::string user_name;
        std::string password;
        if (!json_utils::ReadField(parsed, "user_name", &user_name) ||
            !json_utils::ReadField(parsed, "password", &password)) {
            return StatusResponse(400, "Invalid login request");
        }

        LoginRequest login_request;
        login_request.context = access_->MakeContext(request, nullptr);
        login_request.user_name = user_name;
        login_request.password = password;
        LoginResult login = auth_service_->Login(login_request);
        if (login.token.empty()) {
            access_->IncrementAuthFailures();
            return StatusResponse(401, "Unauthorized");
        }

        ConfigJson root = ConfigJson::object();
        root["token"] = login.token;
        root["expires_at_ms"] = login.expires_at_ms;
        root["principal"] = PrincipalToJson(login.principal);
        return JsonResponse(200, root);
    }

    HttpResponse HandleLogout(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireAuth(access_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        live_stream::RequestContext request_context =
            access_->MakeContext(request, &principal);
        if (!auth_service_->Logout(request_context)) {
            return StatusResponse(404, "Not Found");
        }
        return OkResponse();
    }

    HttpResponse HandleMe(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireAuth(access_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        return JsonResponse(200, PrincipalToJson(principal));
    }

    HttpAccess *access_ = nullptr;
    IAuthService *auth_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateAuthHttpHandler(
    HttpAccess *access, IAuthService *auth_service) {
    return std::unique_ptr<IHttpHandler>(
        new AuthHttpHandler(access, auth_service));
}

}  // namespace live_stream
