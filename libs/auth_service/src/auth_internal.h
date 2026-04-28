#ifndef LIVE_STREAM_AUTH_SERVICE_SRC_AUTH_INTERNAL_H_
#define LIVE_STREAM_AUTH_SERVICE_SRC_AUTH_INTERNAL_H_

#include "auth_service.h"

#include "infra/status.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace auth_internal {

constexpr std::size_t kMaxUserNameLength = 64;
constexpr std::size_t kMaxPasswordLength = 256;
constexpr std::size_t kMaxTokenLength = 256;
constexpr std::size_t kMaxTargetLength = 128;
constexpr std::size_t kMaxAuthConfigSize = 64 * 1024;

struct SessionRecord {
    AuthPrincipal principal;
    std::string token;
    int64_t expires_at_monotonic_ms = 0;
    int64_t expires_at_ms = 0;
};

bool IsEmptyOrTooLong(const std::string& value, std::size_t max_length);
bool IsHexString(const std::string& value);
infra::Result<std::string> Sha256Credential(const std::string& password,
                                             const std::string& salt_hex);
bool ConstantTimeEquals(const std::string& left, const std::string& right);
bool ParseRole(const std::string& role, AuthRole* parsed);
bool RoleHasPermission(AuthRole role, AuthPermission permission);
std::string MakeHexToken(uint64_t a, uint64_t b, uint64_t c, uint64_t d);
std::string MakeSessionId(uint64_t sequence);

}  // namespace auth_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AUTH_SERVICE_SRC_AUTH_INTERNAL_H_
