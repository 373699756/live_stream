#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

namespace live_stream {

class EventStreamHttpHandler : public IHttpHandler {
public:
    explicit EventStreamHttpHandler(HttpAccess *access) : access_(access) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/events/stream",
                              &EventStreamHttpHandler::HandleStreamRoute,
                              this);
    }

private:
    static HttpResponse HandleStreamRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<EventStreamHttpHandler *>(user)->HandleStream(
            request);
    }

    HttpResponse HandleStream(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return StatusResponse(501, "Event stream not implemented");
    }

    HttpAccess *access_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateEventStreamHttpHandler(
    HttpAccess *access) {
    return std::unique_ptr<IHttpHandler>(
        new EventStreamHttpHandler(access));
}

}  // namespace live_stream
