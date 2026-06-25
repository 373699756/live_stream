#include "http_request_id.h"

#include "infra/time.h"

namespace live_stream {

std::string MakeRequestId(uint64_t id) {
    return std::string("http-") +
           std::to_string(infra::Time::SystemTimeMillis()) + "-" +
           std::to_string(id);
}

}  // namespace live_stream
