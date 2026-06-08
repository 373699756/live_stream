#ifndef LIVE_STREAM_ONVIF_SRC_ONVIF_HTTP_H_
#define LIVE_STREAM_ONVIF_SRC_ONVIF_HTTP_H_

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif {

struct OnvifHttpRequest {
    // ONVIF 只使用 device service 的 POST + SOAP body。headers 保留原始块，
    // 供 Basic auth 按大小写不敏感方式查找 Authorization。
    std::string method;
    std::string path;
    std::string headers;
    std::string body;
};

OnvifHttpRequest ParseOnvifHttpRequest(const std::string &raw);
std::string BuildOnvifHttpResponse(uint32_t status_code,
                                   const std::string &reason,
                                   const std::string &body,
                                   const std::string &extra_headers);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SRC_ONVIF_HTTP_H_
