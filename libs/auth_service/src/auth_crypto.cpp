#include "auth_internal.h"

#include <cctype>

#include "infra/hash.h"

namespace live_stream {
namespace auth_internal {
namespace {

uint8_t HexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    return 0;
}

std::string HexToBytes(const std::string& hex) {
    if (!IsHexString(hex)) {
        return std::string();
    }
    std::string bytes;
    bytes.resize(hex.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>((HexNibble(hex[i * 2]) << 4) |
                                     HexNibble(hex[i * 2 + 1]));
    }
    return bytes;
}

}  // namespace

bool IsHexString(const std::string& value) {
    return infra::IsHexString(value);
}

std::string Sha256Credential(
    const std::string& password,
    const std::string& salt_hex) {
    if (password.empty() || password.size() > kMaxPasswordLength ||
        !IsHexString(salt_hex)) {
        return std::string();
    }
    std::string salt = HexToBytes(salt_hex);
    if (salt.empty()) {
        return std::string();
    }
    return "sha256:" + salt_hex + ":" + infra::Sha256Hex(salt + password);
}

bool ConstantTimeEquals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return diff == 0;
}

}  // namespace auth_internal
}  // namespace live_stream
