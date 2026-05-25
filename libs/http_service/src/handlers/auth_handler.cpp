#include "handlers/http_handlers.h"

#include "http_handler_utils.h"
#include "http_request_utils.h"

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
    root["must_change_password"] = principal.must_change_password;
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
        router->AddExactRoute(HttpMethod::kPost, "/api/auth/change-password",
                              &AuthHttpHandler::HandleChangePasswordRoute,
                              this);
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

    static HttpResponse HandleChangePasswordRoute(void *user,
                                                  const HttpRequest &request) {
        return static_cast<AuthHttpHandler *>(user)->HandleChangePassword(
            request);
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
        root["must_change_password"] = login.must_change_password;
        root["principal"] = PrincipalToJson(login.principal);
        HttpResponse response = JsonResponse(200, root);
        response.headers["Set-Cookie"] =
            BuildSessionCookie(login.token, login.expires_at_ms);
        return response;
    }

    HttpResponse HandleLogout(const HttpRequest &request) {
        AuthPrincipal principal = access_->Authenticate(request);
        if (principal.user_name.empty()) {
            return StatusResponse(401, "Unauthorized");
        }
        live_stream::RequestContext request_context =
            access_->MakeContext(request, &principal);
        if (!auth_service_->Logout(request_context)) {
            return StatusResponse(404, "Not Found");
        }
        HttpResponse response = OkResponse();
        response.headers["Set-Cookie"] = ClearSessionCookie();
        return response;
    }

    HttpResponse HandleChangePassword(const HttpRequest &request) {
        AuthPrincipal principal = access_->Authenticate(request);
        if (principal.user_name.empty()) {
            return StatusResponse(401, "Unauthorized");
        }
        ConfigJson parsed;
        if (!ParseJsonObject(request, &parsed)) {
            access_->IncrementParseFailures();
            return StatusResponse(400, "Invalid JSON");
        }
        std::string old_password;
        std::string new_password;
        if (!json_utils::ReadField(parsed, "old_password", &old_password) ||
            !json_utils::ReadField(parsed, "new_password", &new_password)) {
            return StatusResponse(400, "Invalid change-password request");
        }

        ChangePasswordRequest change_request;
        change_request.context = access_->MakeContext(request, &principal);
        change_request.old_password = old_password;
        change_request.new_password = new_password;
        if (!auth_service_->ChangePassword(change_request)) {
            access_->IncrementAuthFailures();
            return StatusResponse(400, "Could not change password");
        }
        return OkResponse();
    }

    HttpResponse HandleMe(const HttpRequest &request) {
        AuthPrincipal principal = access_->Authenticate(request);
        if (principal.user_name.empty()) {
            return StatusResponse(401, "Unauthorized");
        }
        ConfigJson root = ConfigJson::object();
        root["principal"] = PrincipalToJson(principal);
        root["must_change_password"] = principal.must_change_password;
        return JsonResponse(200, root);
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
