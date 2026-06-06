#include "auth_internal.h"

#include <cctype>
#include <cstdint>

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

std::string Sha256Bytes(const std::string& data) {
    infra::Sha256 sha;
    sha.Update(data.data(), data.size());
    const std::string hex = sha.FinishHex();
    return HexToBytes(hex);
}

std::string HmacSha256Bytes(const std::string& key, const std::string& data) {
    constexpr std::size_t kBlockSize = 64;
    std::string hmac_key = key;
    if (hmac_key.size() > kBlockSize) {
        hmac_key = Sha256Bytes(hmac_key);
    }
    hmac_key.resize(kBlockSize, '\0');

    std::string outer_key_pad(kBlockSize, '\0');
    std::string inner_key_pad(kBlockSize, '\0');
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        outer_key_pad[i] = static_cast<char>(
            static_cast<unsigned char>(hmac_key[i]) ^ 0x5cU);
        inner_key_pad[i] = static_cast<char>(
            static_cast<unsigned char>(hmac_key[i]) ^ 0x36U);
    }

    return Sha256Bytes(outer_key_pad + Sha256Bytes(inner_key_pad + data));
}

void AppendBigEndian32(uint32_t value, std::string* out) {
    if (out == nullptr) {
        return;
    }
    out->push_back(static_cast<char>((value >> 24) & 0xffU));
    out->push_back(static_cast<char>((value >> 16) & 0xffU));
    out->push_back(static_cast<char>((value >> 8) & 0xffU));
    out->push_back(static_cast<char>(value & 0xffU));
}

std::string Pbkdf2Sha256Bytes(const std::string& password,
                              const std::string& salt,
                              uint32_t iterations) {
    if (password.empty() || salt.empty() || iterations == 0) {
        return std::string();
    }
    std::string block_input = salt;
    AppendBigEndian32(1, &block_input);
    std::string block = HmacSha256Bytes(password, block_input);
    if (block.empty()) {
        return std::string();
    }
    std::string derived = block;
    for (uint32_t i = 1; i < iterations; ++i) {
        block = HmacSha256Bytes(password, block);
        if (block.size() != derived.size()) {
            return std::string();
        }
        for (std::size_t j = 0; j < derived.size(); ++j) {
            derived[j] = static_cast<char>(
                static_cast<unsigned char>(derived[j]) ^
                static_cast<unsigned char>(block[j]));
        }
    }
    return derived;
}

}  // namespace

bool IsHexString(const std::string& value) {
    return infra::IsHexString(value);
}

std::string Pbkdf2Sha256Credential(const std::string& password,
                                   const std::string& salt_hex,
                                   uint32_t iterations) {
    if (password.empty() || password.size() > kMaxPasswordLength ||
        iterations == 0 || !IsHexString(salt_hex)) {
        return std::string();
    }
    const std::string salt = HexToBytes(salt_hex);
    if (salt.empty()) {
        return std::string();
    }
    const std::string derived =
        Pbkdf2Sha256Bytes(password, salt, iterations);
    if (derived.empty()) {
        return std::string();
    }
    return "pbkdf2-sha256:" + std::to_string(iterations) + ":" + salt_hex +
           ":" + infra::BytesToHex(
                     reinterpret_cast<const uint8_t*>(derived.data()),
                     derived.size());
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
