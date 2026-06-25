#ifndef LIVE_STREAM_HTTP_SRC_HTTP_JSON_BODY_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_JSON_BODY_H_

#include "json.h"
#include "http.h"

namespace live_stream {

bool ParseJsonObject(const HttpRequest &request, Json *body);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_JSON_BODY_H_
