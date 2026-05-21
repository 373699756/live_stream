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

class AuthHttpHandler : public IHttpHandler {
public:
    AuthHttpHandler(HttpHandlerContext *context,
                    const AuthHandlerDependencies &dependencies)
        : context_(context), dependencies_(dependencies) {}

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
            context_->IncrementParseFailures();
            return StatusResponse(400, "Invalid JSON");
        }
        std::string user_name;
        std::string password;
        if (!json_utils::ReadField(parsed, "user_name", &user_name) ||
            !json_utils::ReadField(parsed, "password", &password)) {
            return StatusResponse(400, "Invalid login request");
        }

        LoginRequest login_request;
        login_request.context = context_->MakeContext(request, nullptr);
        login_request.user_name = user_name;
        login_request.password = password;
        LoginResult login = dependencies_.auth_service->Login(login_request);
        if (login.token.empty()) {
            context_->IncrementAuthFailures();
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
        if (!RequireAuth(context_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        live_stream::RequestContext request_context =
            context_->MakeContext(request, &principal);
        if (!dependencies_.auth_service->Logout(request_context)) {
            return StatusResponse(404, "Not Found");
        }
        return OkResponse();
    }

    HttpResponse HandleMe(const HttpRequest &request) {
        AuthPrincipal principal;
        if (!RequireAuth(context_, request, &principal)) {
            return StatusResponse(401, "Unauthorized");
        }
        return JsonResponse(200, PrincipalToJson(principal));
    }

    HttpHandlerContext *context_ = nullptr;
    AuthHandlerDependencies dependencies_;
};

std::unique_ptr<IHttpHandler> CreateAuthHttpHandler(
    HttpHandlerContext *context, const AuthHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new AuthHttpHandler(context, dependencies));
}

}  // namespace live_stream
