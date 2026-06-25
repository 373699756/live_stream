#ifndef LIVE_STREAM_HTTP_HTTP_ROUTER_H_
#define LIVE_STREAM_HTTP_HTTP_ROUTER_H_

#include "http.h"

#include <functional>
#include <string>
#include <vector>

namespace live_stream {

using HttpRouteCallback = std::function<HttpResponse(const HttpRequest &)>;

class IHttpRouter {
public:
    virtual ~IHttpRouter() = default;

    virtual void AddExactRoute(HttpMethod method, const char *path,
                               HttpRouteCallback callback) = 0;
    virtual void AddPrefixRoute(HttpMethod method, const char *path,
                                HttpRouteCallback callback) = 0;

    template <typename Handler>
    void AddExactRoute(HttpMethod method, const char *path, Handler *handler,
                       HttpResponse (Handler::*callback)(
                           const HttpRequest &request)) {
        AddExactRoute(method, path,
                      [handler, callback](const HttpRequest &request) {
                          return (handler->*callback)(request);
                      });
    }

    template <typename Handler>
    void AddPrefixRoute(HttpMethod method, const char *path, Handler *handler,
                        HttpResponse (Handler::*callback)(
                            const HttpRequest &request)) {
        AddPrefixRoute(method, path,
                       [handler, callback](const HttpRequest &request) {
                           return (handler->*callback)(request);
                       });
    }
};

struct HttpRouteMatch {
    bool found = false;
    HttpRouteCallback callback;
};

class HttpRouter : public IHttpRouter {
public:
    void AddExactRoute(HttpMethod method, const char *path,
                       HttpRouteCallback callback) override;
    void AddPrefixRoute(HttpMethod method, const char *path,
                        HttpRouteCallback callback) override;
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
        HttpRouteCallback callback;
    };

    void AddRoute(HttpMethod method, const char *path, MatchType match_type,
                  HttpRouteCallback callback);

    std::vector<Route> routes_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_ROUTER_H_
