#ifndef LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_AUTH_H_
#define LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_AUTH_H_

#include "auth.h"
#include "http.h"
#include "http_access.h"

namespace live_stream {

HttpResponse RequireHttpMediaAuthResponse(HttpAccess *access,
                                          const HttpRequest &request,
                                          AuthPrincipal *principal);
HttpResponse RequireLiveStreamAuthResponse(HttpAccess *access,
                                           const HttpRequest &request,
                                           AuthPrincipal *principal);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_AUTH_H_
