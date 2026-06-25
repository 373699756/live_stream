#ifndef LIVE_STREAM_HTTP_SRC_HTTP_AUTH_SESSION_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_AUTH_SESSION_H_

#include "http.h"

#include <cstdint>
#include <string>

namespace live_stream {

std::string ExtractAuthToken(const HttpRequest &request);
std::string BuildSessionCookie(const std::string &token, int64_t expires_at_ms);
std::string ClearSessionCookie();

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_AUTH_SESSION_H_
