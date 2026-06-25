#include "http_media_json_body.h"

namespace live_stream {

bool ParseOptionalHttpMediaJsonBody(const HttpRequest &request,
                                    Json *body) {
    if (body == nullptr) {
        return false;
    }
    if (request.body.empty()) {
        *body = Json::object();
        return true;
    }
    *body = Json::parse(request.body, nullptr, false);
    return !body->is_discarded() && body->is_object();
}

}  // namespace live_stream
