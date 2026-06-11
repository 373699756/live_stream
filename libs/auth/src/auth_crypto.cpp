#include "auth_internal.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "infra/hash.h"

namespace live_stream {
namespace auth_internal {
namespace {

constexpr std::size_t kSha256BlockSize = 64;
constexpr std::size_t kSha256DigestSize = 32;

struct HmacSha256Key {
    infra::Sha256 inner_seed;
    infra::Sha256 outer_seed;
};

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

bool Sha256Bytes(const uint8_t* data, std::size_t size,
                 uint8_t digest[kSha256DigestSize]) {
    infra::Sha256 sha;
    sha.Update(data, size);
    return sha.Finish(digest, kSha256DigestSize);
}

bool PrepareHmacSha256Key(const uint8_t* key, std::size_t key_size,
                          HmacSha256Key* prepared_key) {
    if (key == nullptr || prepared_key == nullptr) {
        return false;
    }

    uint8_t hmac_key[kSha256BlockSize] = {0};
    if (key_size > kSha256BlockSize) {
        if (!Sha256Bytes(key, key_size, hmac_key)) {
            return false;
        }
    } else if (key_size > 0) {
        std::memcpy(hmac_key, key, key_size);
    }

    uint8_t outer_key_pad[kSha256BlockSize] = {0};
    uint8_t inner_key_pad[kSha256BlockSize] = {0};
    for (std::size_t i = 0; i < kSha256BlockSize; ++i) {
        outer_key_pad[i] = hmac_key[i] ^ 0x5cU;
        inner_key_pad[i] = hmac_key[i] ^ 0x36U;
    }

    prepared_key->inner_seed.Update(inner_key_pad, sizeof(inner_key_pad));
    prepared_key->outer_seed.Update(outer_key_pad, sizeof(outer_key_pad));
    return true;
}

bool HmacSha256Bytes(const HmacSha256Key& key, const uint8_t* data,
                     std::size_t data_size,
                     uint8_t digest[kSha256DigestSize]) {
    if (data == nullptr || digest == nullptr) {
        return false;
    }

    uint8_t inner_digest[kSha256DigestSize] = {0};
    infra::Sha256 inner_sha = key.inner_seed;
    inner_sha.Update(data, data_size);
    if (!inner_sha.Finish(inner_digest, sizeof(inner_digest))) {
        return false;
    }

    infra::Sha256 outer_sha = key.outer_seed;
    outer_sha.Update(inner_digest, sizeof(inner_digest));
    return outer_sha.Finish(digest, kSha256DigestSize);
}

void WriteBigEndian32(uint32_t value, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>((value >> 24) & 0xffU);
    out[1] = static_cast<uint8_t>((value >> 16) & 0xffU);
    out[2] = static_cast<uint8_t>((value >> 8) & 0xffU);
    out[3] = static_cast<uint8_t>(value & 0xffU);
}

std::string Pbkdf2Sha256Bytes(const std::string& password,
                              const std::string& salt,
                              uint32_t iterations) {
    if (password.empty() || salt.empty() || iterations == 0) {
        return std::string();
    }
    std::string block_input = salt;
    const std::size_t salt_size = block_input.size();
    block_input.resize(salt_size + 4);
    WriteBigEndian32(1,
                     reinterpret_cast<uint8_t*>(&block_input[0]) + salt_size);

    HmacSha256Key hmac_key;
    if (!PrepareHmacSha256Key(
            reinterpret_cast<const uint8_t*>(password.data()),
            password.size(), &hmac_key)) {
        return std::string();
    }

    uint8_t block[kSha256DigestSize] = {0};
    uint8_t derived[kSha256DigestSize] = {0};
    if (!HmacSha256Bytes(
            hmac_key,
            reinterpret_cast<const uint8_t*>(block_input.data()),
            block_input.size(), block)) {
        return std::string();
    }
    std::memcpy(derived, block, sizeof(derived));
    for (uint32_t i = 1; i < iterations; ++i) {
        if (!HmacSha256Bytes(
                hmac_key, block, sizeof(block), block)) {
            return std::string();
        }
        for (std::size_t j = 0; j < sizeof(derived); ++j) {
            derived[j] ^= block[j];
        }
    }
    return std::string(reinterpret_cast<const char*>(derived),
                       sizeof(derived));
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
           ":" + infra::BytesToHex(reinterpret_cast<const uint8_t*>(derived.data()), derived.size());
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
