#include "http_media_auth.h"

#include "http_media_response.h"

namespace live_stream {

HttpResponse RequireHttpMediaAuthResponse(HttpAccess *access,
                                          const HttpRequest &request,
                                          AuthPrincipal *principal) {
    if (access == nullptr || principal == nullptr) {
        return HttpMediaStatusResponse(401, "Unauthorized");
    }
    *principal = access->Authenticate(request);
    if (principal->user_name.empty()) {
        return HttpMediaStatusResponse(401, "Unauthorized");
    }
    if (principal->must_change_password) {
        return HttpMediaForbiddenResponse(*principal);
    }
    HttpResponse response;
    response.status_code = 0;
    return response;
}

HttpResponse RequireLiveStreamAuthResponse(
    HttpAccess *access, const HttpRequest &request,
    AuthPrincipal *principal) {
    HttpResponse response =
        RequireHttpMediaAuthResponse(access, request, principal);
    if (response.status_code == 0) {
        return response;
    }
    if (response.status_code == 403 && principal != nullptr &&
        principal->must_change_password) {
        return HttpMediaTextResponse(403, "must_change_password");
    }
    if (response.status_code == 401) {
        return HttpMediaTextResponse(401, "Unauthorized");
    }
    return HttpMediaTextResponse(response.status_code, "Forbidden");
}

}  // namespace live_stream
