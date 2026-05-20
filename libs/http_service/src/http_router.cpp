#include "http_router.h"

#include "handlers/http_handlers.h"
#include "http_handler_utils.h"

namespace live_stream {
namespace {

enum class HttpRouteMatchType {
    kExact,
    kPrefix,
};

struct HttpRoute {
    HttpMethod method = HttpMethod::kGet;
    const char *path = nullptr;
    HttpRouteMatchType match_type = HttpRouteMatchType::kExact;
    HttpRouteHandler handler = nullptr;
};

HttpResponse HandleMediaCapabilitiesRoute(HttpHandlerContext *context,
                                          const HttpRequest &request) {
    (void)request;
    return http_handlers::HandleMediaCapabilities(context);
}

bool MatchesRoute(const HttpRoute &route, const HttpRequest &request) {
    if (route.path == nullptr || route.handler == nullptr ||
        route.method != request.method) {
        return false;
    }
    if (route.match_type == HttpRouteMatchType::kExact) {
        return request.path == route.path;
    }
    return StartsWith(request.path, route.path);
}

}  // namespace

HttpRouteMatch MatchHttpRoute(const HttpRequest &request) {
    static const HttpRoute kRoutes[] = {
        {HttpMethod::kPost, "/api/auth/login", HttpRouteMatchType::kExact,
         http_handlers::HandleLogin},
        {HttpMethod::kPost, "/api/auth/logout", HttpRouteMatchType::kExact,
         http_handlers::HandleLogout},
        {HttpMethod::kGet, "/api/auth/me", HttpRouteMatchType::kExact,
         http_handlers::HandleMe},
        {HttpMethod::kGet, "/api/media/capabilities",
         HttpRouteMatchType::kExact, HandleMediaCapabilitiesRoute},
        {HttpMethod::kGet, "/api/status/streams", HttpRouteMatchType::kExact,
         http_handlers::HandleStreamStatus},
        {HttpMethod::kGet, "/api/system/status", HttpRouteMatchType::kExact,
         http_handlers::HandleSystemStatus},
        {HttpMethod::kGet, "/api/system/capabilities",
         HttpRouteMatchType::kExact, http_handlers::HandleSystemCapabilities},
        {HttpMethod::kPost, "/api/system/reboot", HttpRouteMatchType::kExact,
         http_handlers::HandleSystemReboot},
        {HttpMethod::kPost, "/api/system/factory-reset",
         HttpRouteMatchType::kExact, http_handlers::HandleSystemFactoryReset},
        {HttpMethod::kGet, "/api/time/status", HttpRouteMatchType::kExact,
         http_handlers::HandleTimeStatus},
        {HttpMethod::kPut, "/api/time/timezone", HttpRouteMatchType::kExact,
         http_handlers::HandleTimeTimezone},
        {HttpMethod::kPut, "/api/time/ntp", HttpRouteMatchType::kExact,
         http_handlers::HandleTimeNtp},
        {HttpMethod::kPost, "/api/time/system-time",
         HttpRouteMatchType::kExact, http_handlers::HandleTimeSystemTime},
        {HttpMethod::kPost, "/api/time/sync", HttpRouteMatchType::kExact,
         http_handlers::HandleTimeSync},
        {HttpMethod::kGet, "/api/network/interfaces",
         HttpRouteMatchType::kExact, http_handlers::HandleNetworkInterfaces},
        {HttpMethod::kGet, "/api/network/interfaces/",
         HttpRouteMatchType::kPrefix, http_handlers::HandleNetworkInterface},
        {HttpMethod::kPut, "/api/network/interfaces/",
         HttpRouteMatchType::kPrefix, http_handlers::HandleNetworkInterface},
        {HttpMethod::kPost, "/api/network/reload", HttpRouteMatchType::kExact,
         http_handlers::HandleNetworkReload},
        {HttpMethod::kPost, "/api/upgrade/upload", HttpRouteMatchType::kExact,
         http_handlers::HandleUpgradeUpload},
        {HttpMethod::kGet, "/api/upgrade/status", HttpRouteMatchType::kExact,
         http_handlers::HandleUpgradeStatus},
        {HttpMethod::kPost, "/api/upgrade/validate",
         HttpRouteMatchType::kExact, http_handlers::HandleUpgradeValidate},
        {HttpMethod::kPost, "/api/upgrade/start", HttpRouteMatchType::kExact,
         http_handlers::HandleUpgradeStart},
        {HttpMethod::kPost, "/api/upgrade/cancel", HttpRouteMatchType::kExact,
         http_handlers::HandleUpgradeCancel},
        {HttpMethod::kPost, "/api/upgrade/confirm-reboot",
         HttpRouteMatchType::kExact, http_handlers::HandleUpgradeConfirmReboot},
        {HttpMethod::kGet, "/api/ai/status", HttpRouteMatchType::kExact,
         http_handlers::HandleAiStatus},
        {HttpMethod::kGet, "/api/snapshot/", HttpRouteMatchType::kPrefix,
         http_handlers::HandleSnapshot},
        {HttpMethod::kGet, "/api/hls/", HttpRouteMatchType::kPrefix,
         http_handlers::HandleHls},
        {HttpMethod::kPost, "/api/webrtc", HttpRouteMatchType::kPrefix,
         http_handlers::HandleWebrtc},
        {HttpMethod::kDelete, "/api/webrtc", HttpRouteMatchType::kPrefix,
         http_handlers::HandleWebrtc},
        {HttpMethod::kGet, "/api/config/", HttpRouteMatchType::kPrefix,
         http_handlers::HandleConfig},
        {HttpMethod::kPut, "/api/config/", HttpRouteMatchType::kPrefix,
         http_handlers::HandleConfig},
        {HttpMethod::kGet, "/api/operations/export",
         HttpRouteMatchType::kExact, http_handlers::HandleOperationsExport},
        {HttpMethod::kGet, "/api/operations", HttpRouteMatchType::kExact,
         http_handlers::HandleOperations},
    };

    for (const HttpRoute &route : kRoutes) {
        if (MatchesRoute(route, request)) {
            HttpRouteMatch match;
            match.found = true;
            match.handler = route.handler;
            return match;
        }
    }
    return HttpRouteMatch{};
}

}  // namespace live_stream
