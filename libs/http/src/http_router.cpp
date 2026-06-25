#include "http_router.h"

#include "http_path.h"

#include <utility>

namespace live_stream {

void HttpRouter::AddExactRoute(HttpMethod method, const char *path,
                               HttpRouteCallback callback) {
    AddRoute(method, path, MatchType::kExact, std::move(callback));
}

void HttpRouter::AddPrefixRoute(HttpMethod method, const char *path,
                                HttpRouteCallback callback) {
    AddRoute(method, path, MatchType::kPrefix, std::move(callback));
}

HttpRouteMatch HttpRouter::Match(const HttpRequest &request) const {
    for (const Route &route : routes_) {
        if (!route.callback || route.method != request.method) {
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
            return match;
        }
    }
    return HttpRouteMatch{};
}

void HttpRouter::Clear() {
    routes_.clear();
}

void HttpRouter::AddRoute(HttpMethod method, const char *path,
                          MatchType match_type,
                          HttpRouteCallback callback) {
    if (path == nullptr || !callback) {
        return;
    }
    Route route;
    route.method = method;
    route.path = path;
    route.match_type = match_type;
    route.callback = std::move(callback);
    routes_.push_back(route);
}

}  // namespace live_stream
