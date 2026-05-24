#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_PROTOCOL_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_PROTOCOL_H_

#include "http_service.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {

enum class RawParseStatus {
    kComplete,
    kIncomplete,
    kBadRequest,
    kPayloadTooLarge,
};

struct RawParseResult {
    RawParseStatus status = RawParseStatus::kBadRequest;
    HttpRequest request;
    size_t consumed_bytes = 0;
    bool keep_alive = false;
};

std::string ToLower(const std::string& value);
std::string Trim(const std::string& value);
std::string GetHeader(const HttpRequest& request, const std::string& name);
std::string SerializeResponseHeader(const HttpResponse& response);
std::string SerializeResponse(const HttpResponse& response);
RawParseResult ParseRawRequest(const std::string& raw,
                               uint32_t max_header_bytes,
                               uint32_t max_body_bytes,
                               const std::string& client_ip);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_PROTOCOL_H_
