#include "http_request_utils.h"

#include "http_protocol.h"
#include "infra/time.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

constexpr const char *kSessionCookieName = "live_stream_token";

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

std::string ExtractCookieValue(const HttpRequest &request,
                               const std::string &name) {
    const std::string cookie_header = GetHeader(request, "Cookie");
    std::size_t begin = 0;
    while (begin < cookie_header.size()) {
        while (begin < cookie_header.size() &&
               (cookie_header[begin] == ' ' || cookie_header[begin] == ';')) {
            ++begin;
        }
        const std::size_t end = cookie_header.find(';', begin);
        const std::string part = Trim(cookie_header.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
        const std::size_t equal = part.find('=');
        if (equal != std::string::npos &&
            Trim(part.substr(0, equal)) == name) {
            return Trim(part.substr(equal + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::string();
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
    const std::string cookie_token =
        ExtractCookieValue(request, kSessionCookieName);
    if (!cookie_token.empty()) {
        return cookie_token;
    }
    return std::string();
}

std::string BuildSessionCookie(const std::string &token, int64_t expires_at_ms) {
    if (token.empty()) {
        return std::string();
    }
    std::string cookie = std::string(kSessionCookieName) + "=" + token +
                         "; Path=/; HttpOnly; SameSite=Strict";
    const int64_t max_age_ms = expires_at_ms - infra::Time::SystemTimeMillis();
    if (max_age_ms > 0) {
        cookie += "; Max-Age=" + std::to_string(max_age_ms / 1000);
    }
    return cookie;
}

std::string ClearSessionCookie() {
    return std::string(kSessionCookieName) +
           "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
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
