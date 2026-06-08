#include "onvif_http.h"

namespace live_stream {
namespace onvif {

OnvifHttpRequest ParseOnvifHttpRequest(const std::string &raw) {
    // ONVIF device service 使用简单的 SOAP over HTTP POST。这里按一次完整
    // TCP message 解析，不实现 keep-alive pipeline，响应后由 server 关闭连接。
    const std::size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return OnvifHttpRequest();
    }
    const std::string request_line = raw.substr(0, line_end);
    const std::size_t first_space = request_line.find(' ');
    const std::size_t second_space =
        first_space == std::string::npos
            ? std::string::npos
            : request_line.find(' ', first_space + 1);
    if (first_space == std::string::npos ||
        second_space == std::string::npos) {
        return OnvifHttpRequest();
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return OnvifHttpRequest();
    }

    OnvifHttpRequest request;
    request.method = request_line.substr(0, first_space);
    request.path =
        request_line.substr(first_space + 1, second_space - first_space - 1);
    request.headers = raw.substr(line_end + 2, header_end - line_end - 2);
    request.body = raw.substr(header_end + 4);
    if (request.method.empty() || request.path.empty()) {
        return OnvifHttpRequest();
    }
    return request;
}

std::string BuildOnvifHttpResponse(uint32_t status_code,
                                   const std::string &reason,
                                   const std::string &body,
                                   const std::string &extra_headers) {
    // ONVIF 客户端兼容性优先：每个 SOAP 响应带明确 Content-Length 并关闭连接，
    // 避免不同 NVR 对 persistent HTTP 的处理差异。
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " +
                           reason + "\r\n";
    response += "Content-Type: application/soap+xml; charset=utf-8\r\n";
    response += extra_headers;
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

}  // namespace onvif
}  // namespace live_stream
