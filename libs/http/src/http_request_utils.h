#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_REQUEST_UTILS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_REQUEST_UTILS_H_

#include "http.h"

#include <cstdint>
#include <string>

namespace live_stream {

std::string RequestUserAgent(const HttpRequest &request);
std::string MakeRequestId(uint64_t id);
std::string ExtractBearerToken(const HttpRequest &request);
std::string BuildSessionCookie(const std::string &token, int64_t expires_at_ms);
std::string ClearSessionCookie();
std::string DecodeUrlComponent(const std::string &value);
std::string QueryValue(const HttpRequest &request, const std::string &name);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_REQUEST_UTILS_H_
