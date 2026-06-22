#ifndef LIVE_STREAM_AUTH_SRC_AUTH_INTERNAL_H_
#define LIVE_STREAM_AUTH_SRC_AUTH_INTERNAL_H_

#include "auth.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace auth_internal {

constexpr std::size_t kMaxUserNameLength = kMaxAuthUserNameLength;
constexpr std::size_t kMaxPasswordLength = kMaxAuthPasswordLength;
constexpr std::size_t kMaxTokenLength = 256;
constexpr std::size_t kMaxTargetLength = 128;
constexpr uint32_t kPasswordPbkdf2Iterations = 20000;

struct SessionRecord {
    AuthPrincipal principal;
    std::string token;
    int64_t created_at_monotonic_ms = 0;
    int64_t expires_at_monotonic_ms = 0;
    int64_t expires_at_ms = 0;
    bool must_change_password = false;
};

bool IsEmptyOrTooLong(const std::string& value, std::size_t max_length);
bool IsHexString(const std::string& value);
std::string Pbkdf2Sha256Credential(const std::string& password,
                                   const std::string& salt_hex,
                                   uint32_t iterations);
bool ConstantTimeEquals(const std::string& left, const std::string& right);
bool ParseRole(const std::string& role, AuthRole* parsed);
bool IsPermissionAllowed(AuthRole role, AuthPermission permission);
std::string MakeHexToken(uint64_t a, uint64_t b, uint64_t c, uint64_t d);

}  // namespace auth_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AUTH_SRC_AUTH_INTERNAL_H_
