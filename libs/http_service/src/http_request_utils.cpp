#include "http_request_utils.h"

#include "http_protocol.h"
#include "infra/time.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

}  // namespace

std::string RequestUserAgent(const HttpRequest &request) {
    return GetHeader(request, "User-Agent");
}

std::string MakeRequestId(uint64_t id) {
    return std::string("http-") +
           std::to_string(infra::Time::SystemTimeMillis()) + "-" +
           std::to_string(id);
}

std::string ExtractBearerToken(const HttpRequest &request) {
    const std::string authorization = Trim(GetHeader(request, "Authorization"));
    const std::string prefix = "bearer ";
    if (authorization.size() > prefix.size() &&
        ToLower(authorization.substr(0, prefix.size())) == prefix) {
        return Trim(authorization.substr(prefix.size()));
    }
    const std::string key = "token=";
    std::size_t pos = request.query_string.find(key);
    if (pos == std::string::npos) {
        return std::string();
    }
    pos += key.size();
    const std::size_t end = request.query_string.find('&', pos);
    return request.query_string.substr(
        pos, end == std::string::npos ? std::string::npos : end - pos);
}

std::string DecodeUrlComponent(const std::string &value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (c == '%' && i + 2 < value.size()) {
            const int high = HexValue(value[i + 1]);
            const int low = HexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        decoded.push_back(c);
    }
    return decoded;
}

std::string QueryValue(const HttpRequest &request, const std::string &name) {
    std::size_t begin = 0;
    while (begin <= request.query_string.size()) {
        const std::size_t end = request.query_string.find('&', begin);
        const std::string part = request.query_string.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        const std::size_t equal = part.find('=');
        const std::string key = DecodeUrlComponent(
            part.substr(0, equal == std::string::npos ? part.size() : equal));
        if (key == name) {
            return equal == std::string::npos
                       ? std::string()
                       : DecodeUrlComponent(part.substr(equal + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::string();
}

}  // namespace live_stream
