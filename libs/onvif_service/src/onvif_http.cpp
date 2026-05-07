#include "onvif_http.h"

namespace live_stream {
namespace onvif_internal {

HttpRequest ParseHttpRequest(const std::string& raw) {
    const std::size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return HttpRequest();
    }
    const std::string request_line = raw.substr(0, line_end);
    const std::size_t first_space = request_line.find(' ');
    const std::size_t second_space =
        first_space == std::string::npos
            ? std::string::npos
            : request_line.find(' ', first_space + 1);
    if (first_space == std::string::npos ||
        second_space == std::string::npos) {
        return HttpRequest();
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return HttpRequest();
    }

    HttpRequest request;
    request.method = request_line.substr(0, first_space);
    request.path =
        request_line.substr(first_space + 1, second_space - first_space - 1);
    request.headers = raw.substr(line_end + 2, header_end - line_end - 2);
    request.body = raw.substr(header_end + 4);
    if (request.method.empty() || request.path.empty()) {
        return HttpRequest();
    }
    return request;
}

std::string HttpResponse(uint32_t status_code,
                         const std::string& reason,
                         const std::string& body,
                         const std::string& extra_headers) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " +
                           reason + "\r\n";
    response += "Content-Type: application/soap+xml; charset=utf-8\r\n";
    response += extra_headers;
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

}  // namespace onvif_internal
}  // namespace live_stream
