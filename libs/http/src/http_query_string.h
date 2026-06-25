#ifndef LIVE_STREAM_HTTP_SRC_HTTP_QUERY_STRING_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_QUERY_STRING_H_

#include "http.h"

#include <string>

namespace live_stream {

std::string DecodeUrlComponent(const std::string &value);
std::string QueryValue(const HttpRequest &request, const std::string &name);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_QUERY_STRING_H_
