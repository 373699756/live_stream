#include "http_protocol.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace live_stream {

namespace {

const char* HttpStatusText(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 409:
            return "Conflict";
        case 413:
            return "Payload Too Large";
        case 500:
            return "Internal Server Status";
        case 501:
            return "Not Implemented";
        case 503:
            return "Service Unavailable";
        default:
            return "Status";
    }
}

HttpMethod ParseMethod(const std::string& value) {
    if (value == "GET") {
        return HttpMethod::kGet;
    }
    if (value == "POST") {
        return HttpMethod::kPost;
    }
    if (value == "PUT") {
        return HttpMethod::kPut;
    }
    if (value == "DELETE") {
        return HttpMethod::kDelete;
    }
    return HttpMethod::kGet;
}

}  // namespace

std::string ToLower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string GetHeader(const HttpRequest& request, const std::string& name) {
    const std::string lower_name = ToLower(name);
    for (const auto& item : request.headers) {
        if (ToLower(item.first) == lower_name) {
            return item.second;
        }
    }
    return std::string();
}

std::string SerializeResponseHeaderWithBodySize(const HttpResponse& response,
                                                size_t body_size) {
    std::string out = "HTTP/1.1 " + std::to_string(response.status_code) + " " +
                      HttpStatusText(response.status_code) + "\r\n";
    bool has_length = false;
    bool has_connection = false;
    for (const auto& header : response.headers) {
        if (ToLower(header.first) == "content-length") {
            has_length = true;
        }
        if (ToLower(header.first) == "connection") {
            has_connection = true;
        }
        out += header.first + ": " + header.second + "\r\n";
    }
    if (!has_length) {
        out += "Content-Length: " + std::to_string(body_size) + "\r\n";
    }
    if (!has_connection) {
        out += "Connection: close\r\n";
    }
    out += "\r\n";
    return out;
}

std::string SerializeResponseHeader(const HttpResponse& response) {
    return SerializeResponseHeaderWithBodySize(response, response.body.size());
}

std::string SerializeResponse(const HttpResponse& response) {
    return SerializeResponseHeader(response) + response.body;
}

RawParseResult ParseRawRequest(const std::string& raw,
                               uint32_t max_header_bytes,
                               uint32_t max_body_bytes,
                               const std::string& client_ip) {
    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        RawParseResult result;
        result.status = raw.size() > max_header_bytes
                            ? RawParseStatus::kPayloadTooLarge
                            : RawParseStatus::kIncomplete;
        return result;
    }
    if (header_end > max_header_bytes) {
        RawParseResult result;
        result.status = RawParseStatus::kPayloadTooLarge;
        return result;
    }
    const std::string header_block = raw.substr(0, header_end);
    const std::string body = raw.substr(header_end + 4);
    const size_t first_line_end = header_block.find("\r\n");
    const std::string request_line =
        first_line_end == std::string::npos
            ? header_block
            : header_block.substr(0, first_line_end);
    const size_t method_end = request_line.find(' ');
    const size_t target_end =
        method_end == std::string::npos ? std::string::npos
                                        : request_line.find(' ', method_end + 1);
    if (method_end == std::string::npos || target_end == std::string::npos) {
        RawParseResult result;
        result.status = RawParseStatus::kBadRequest;
        return result;
    }
    const std::string method_text = request_line.substr(0, method_end);
    if (method_text != "GET" && method_text != "POST" &&
        method_text != "PUT" && method_text != "DELETE") {
        RawParseResult result;
        result.status = RawParseStatus::kBadRequest;
        return result;
    }
    const std::string version = request_line.substr(target_end + 1);
    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        RawParseResult result;
        result.status = RawParseStatus::kBadRequest;
        return result;
    }

    HttpRequest request;
    request.method = ParseMethod(method_text);
    request.client_ip = client_ip;
    const std::string target =
        request_line.substr(method_end + 1, target_end - method_end - 1);
    const size_t query_pos = target.find('?');
    request.path = query_pos == std::string::npos ? target : target.substr(0, query_pos);
    request.query_string =
        query_pos == std::string::npos ? std::string() : target.substr(query_pos + 1);

    size_t pos = first_line_end == std::string::npos ? header_block.size()
                                                     : first_line_end + 2;
    while (pos < header_block.size()) {
        const size_t line_end = header_block.find("\r\n", pos);
        const std::string line =
            line_end == std::string::npos ? header_block.substr(pos)
                                          : header_block.substr(pos, line_end - pos);
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            RawParseResult result;
            result.status = RawParseStatus::kBadRequest;
            return result;
        }
        request.headers[Trim(line.substr(0, colon))] = Trim(line.substr(colon + 1));
        if (line_end == std::string::npos) {
            break;
        }
        pos = line_end + 2;
    }

    const std::string content_length_text = GetHeader(request, "Content-Length");
    size_t expected_body_size = 0;
    if (!content_length_text.empty()) {
        char* end = nullptr;
        const unsigned long parsed =
            std::strtoul(content_length_text.c_str(), &end, 10);
        if (end == nullptr || *end != '\0') {
            RawParseResult result;
            result.status = RawParseStatus::kBadRequest;
            return result;
        }
        expected_body_size = static_cast<size_t>(parsed);
        if (expected_body_size > max_body_bytes) {
            RawParseResult result;
            result.status = RawParseStatus::kPayloadTooLarge;
            return result;
        }
        if (body.size() < expected_body_size) {
            RawParseResult result;
            result.status = RawParseStatus::kIncomplete;
            return result;
        }
        request.body = body.substr(0, expected_body_size);
    } else if (body.size() > max_body_bytes) {
        RawParseResult result;
        result.status = RawParseStatus::kPayloadTooLarge;
        return result;
    }
    RawParseResult result;
    result.status = RawParseStatus::kComplete;
    result.request = request;
    result.consumed_bytes = header_end + 4 + request.body.size();
    const std::string connection = ToLower(Trim(GetHeader(request, "Connection")));
    result.keep_alive = version == "HTTP/1.1"
                            ? connection != "close"
                            : connection == "keep-alive";
    return result;
}

}  // namespace live_stream
