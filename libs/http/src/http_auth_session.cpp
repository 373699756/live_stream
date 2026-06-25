#include "http_auth_session.h"

#include "http_protocol.h"
#include "infra/time.h"

namespace live_stream {
namespace {

constexpr const char *kSessionCookieName = "live_stream_token";

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

std::string ExtractAuthToken(const HttpRequest &request) {
    const std::string authorization = Trim(GetHeader(request, "Authorization"));
    const std::string prefix = "bearer ";
    if (authorization.size() > prefix.size() &&
        ToLower(authorization.substr(0, prefix.size())) == prefix) {
        return Trim(authorization.substr(prefix.size()));
    }
    return ExtractCookieValue(request, kSessionCookieName);
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

}  // namespace live_stream
