#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_HTTP_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_HTTP_H_

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif_internal {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string headers;
    std::string body;
};

HttpRequest ParseHttpRequest(const std::string& raw);
std::string HttpResponse(uint32_t status_code,
                         const std::string& reason,
                         const std::string& body,
                         const std::string& extra_headers);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_HTTP_H_
