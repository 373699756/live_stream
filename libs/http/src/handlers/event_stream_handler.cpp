#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

namespace live_stream {

class EventStreamHttpHandler : public IHttpHandler {
public:
    EventStreamHttpHandler() = default;

    void RegisterRoutes(IHttpRouter *router) override {
        (void)router;
    }
};

std::unique_ptr<IHttpHandler> CreateEventStreamHttpHandler(
    HttpAccess *access) {
    (void)access;
    return std::unique_ptr<IHttpHandler>(
        new EventStreamHttpHandler());
}

}  // namespace live_stream
