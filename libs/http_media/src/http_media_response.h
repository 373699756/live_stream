#ifndef LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_RESPONSE_H_
#define LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_RESPONSE_H_

#include "auth.h"
#include "json.h"
#include "http.h"

#include <map>
#include <string>

namespace live_stream {

HttpResponse HttpMediaJsonResponse(int status_code, const Json &value);
HttpResponse HttpMediaStatusResponse(int status_code,
                                     const std::string &reason);
HttpResponse HttpMediaTextResponse(int status_code,
                                   const std::string &reason);
HttpResponse HttpMediaForbiddenResponse(const AuthPrincipal &principal);
HttpResponse HttpMediaOkResponse();
std::string BuildHttpMediaStreamHeader(
    int status_code, const std::map<std::string, std::string> &headers);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_RESPONSE_H_
