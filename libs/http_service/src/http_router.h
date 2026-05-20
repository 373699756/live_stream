#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ROUTER_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ROUTER_H_

#include "http_handler_context.h"

namespace live_stream {

using HttpRouteHandler = HttpResponse (*)(
    HttpHandlerContext *context, const HttpRequest &request);

struct HttpRouteMatch {
    bool found = false;
    HttpRouteHandler handler = nullptr;
};

HttpRouteMatch MatchHttpRoute(const HttpRequest &request);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ROUTER_H_
