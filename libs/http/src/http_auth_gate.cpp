#include "http_auth_gate.h"

#include "http_response.h"

namespace live_stream {

HttpResponse RequireAuthResponse(HttpAccess *access,
                                 const HttpRequest &request,
                                 AuthPrincipal *principal) {
    if (access == nullptr || principal == nullptr) {
        return ErrorResponse(401, HttpErrorCode::kUnauthenticated,
                             "Unauthorized");
    }
    *principal = access->Authenticate(request);
    if (principal->user_name.empty()) {
        return ErrorResponse(401, HttpErrorCode::kUnauthenticated,
                             "Unauthorized");
    }
    if (principal->must_change_password) {
        return ForbiddenResponse(*principal);
    }
    HttpResponse response;
    response.status_code = 0;
    return response;
}

HttpResponse RequirePermissionResponse(HttpAccess *access,
                                       const HttpRequest &request,
                                       AuthPermission permission,
                                       const std::string &target,
                                       AuthPrincipal *principal) {
    if (principal != nullptr) {
        *principal = AuthPrincipal{};
    }
    if (access == nullptr ||
        !access->RequirePermission(request, permission, target, principal)) {
        return ForbiddenResponse(principal == nullptr ? AuthPrincipal{}
                                                      : *principal);
    }
    HttpResponse response;
    response.status_code = 0;
    return response;
}

}  // namespace live_stream
