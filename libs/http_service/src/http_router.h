#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ROUTER_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ROUTER_H_

#include "http_service.h"

#include <string>
#include <vector>

namespace live_stream {

using HttpRouteCallback = HttpResponse (*)(void *user,
                                           const HttpRequest &request);

class IHttpRouter {
public:
    virtual ~IHttpRouter() = default;

    virtual void AddExactRoute(HttpMethod method, const char *path,
                               HttpRouteCallback callback, void *user) = 0;
    virtual void AddPrefixRoute(HttpMethod method, const char *path,
                                HttpRouteCallback callback, void *user) = 0;
};

struct HttpRouteMatch {
    bool found = false;
    HttpRouteCallback callback = nullptr;
    void *user = nullptr;
};

class HttpRouter : public IHttpRouter {
public:
    void AddExactRoute(HttpMethod method, const char *path,
                       HttpRouteCallback callback, void *user) override;
    void AddPrefixRoute(HttpMethod method, const char *path,
                        HttpRouteCallback callback, void *user) override;
    HttpRouteMatch Match(const HttpRequest &request) const;
    void Clear();

private:
    enum class MatchType {
        kExact,
        kPrefix,
    };

    struct Route {
        HttpMethod method = HttpMethod::kGet;
        std::string path;
        MatchType match_type = MatchType::kExact;
        HttpRouteCallback callback = nullptr;
        void *user = nullptr;
    };

    void AddRoute(HttpMethod method, const char *path, MatchType match_type,
                  HttpRouteCallback callback, void *user);

    std::vector<Route> routes_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_ROUTER_H_
