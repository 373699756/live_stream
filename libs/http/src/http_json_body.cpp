#include "http_json_body.h"

namespace live_stream {

bool ParseJsonObject(const HttpRequest &request, Json *body) {
    if (body == nullptr) {
        return false;
    }
    *body = Json::parse(request.body, nullptr, false);
    return !body->is_discarded() && body->is_object();
}

}  // namespace live_stream
