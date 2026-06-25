#ifndef LIVE_STREAM_HTTP_SRC_HTTP_AUTH_GATE_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_AUTH_GATE_H_

#include "auth.h"
#include "http.h"
#include "http_access.h"

#include <string>

namespace live_stream {

HttpResponse RequireAuthResponse(HttpAccess *access,
                                 const HttpRequest &request,
                                 AuthPrincipal *principal);
HttpResponse RequirePermissionResponse(HttpAccess *access,
                                       const HttpRequest &request,
                                       AuthPermission permission,
                                       const std::string &target,
                                       AuthPrincipal *principal);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_AUTH_GATE_H_
