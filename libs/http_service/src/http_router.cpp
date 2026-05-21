#include "http_router.h"

#include "http_handler_utils.h"

namespace live_stream {

void HttpRouter::AddExactRoute(HttpMethod method, const char *path,
                               HttpRouteCallback callback, void *user) {
    AddRoute(method, path, MatchType::kExact, callback, user);
}

void HttpRouter::AddPrefixRoute(HttpMethod method, const char *path,
                                HttpRouteCallback callback, void *user) {
    AddRoute(method, path, MatchType::kPrefix, callback, user);
}

HttpRouteMatch HttpRouter::Match(const HttpRequest &request) const {
    for (const Route &route : routes_) {
        if (route.callback == nullptr || route.method != request.method) {
            continue;
        }
        const bool matched =
            route.match_type == MatchType::kExact
                ? request.path == route.path
                : StartsWith(request.path, route.path);
        if (matched) {
            HttpRouteMatch match;
            match.found = true;
            match.callback = route.callback;
            match.user = route.user;
            return match;
        }
    }
    return HttpRouteMatch{};
}

void HttpRouter::Clear() {
    routes_.clear();
}

void HttpRouter::AddRoute(HttpMethod method, const char *path,
                          MatchType match_type, HttpRouteCallback callback,
                          void *user) {
    if (path == nullptr || callback == nullptr) {
        return;
    }
    Route route;
    route.method = method;
    route.path = path;
    route.match_type = match_type;
    route.callback = callback;
    route.user = user;
    routes_.push_back(route);
}

}  // namespace live_stream
