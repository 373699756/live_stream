#ifndef LIVE_STREAM_HTTP_SRC_HTTP_RESPONSE_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_RESPONSE_H_

#include "auth.h"
#include "json.h"
#include "http.h"

#include <string>

namespace live_stream {

enum class HttpErrorCode {
    kInvalidArgument,
    kUnauthenticated,
    kPermissionDenied,
    kStreamNotFound,
    kProtocolUnavailable,
    kPeerNotFound,
    kResourceBusy,
    kInternalError,
};

HttpResponse JsonResponse(int status_code, const Json &value);
HttpResponse ErrorResponse(int status_code, HttpErrorCode code,
                           const std::string &msg);
HttpResponse StatusResponse(int status_code, const std::string &msg);
HttpResponse ForbiddenResponse(const AuthPrincipal &principal);
HttpResponse OkResponse();
HttpResponse AddJsonEnvelope(const HttpRequest &request,
                             const HttpResponse &response);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_RESPONSE_H_
