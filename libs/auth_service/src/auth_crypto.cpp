#include "auth_internal.h"

#include <array>
#include <cctype>
#include <cstring>
#include <vector>

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

std::string BytesToHex(const uint8_t* data, std::size_t size) {
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    hex.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        hex[i * 2] = kHex[(data[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return hex;
}

uint32_t RotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::array<uint8_t, 32> Sha256(const std::string& input) {
    static const uint32_t kInitialHash[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    static const uint32_t kRoundConstants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

    std::vector<uint8_t> message(input.begin(), input.end());
    const uint64_t bit_length = static_cast<uint64_t>(message.size()) * 8;
    message.push_back(0x80U);
    while ((message.size() % 64) != 56) {
        message.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<uint8_t>((bit_length >> (i * 8)) & 0xFF));
    }

    uint32_t hash[8] = {0};
    std::memcpy(hash, kInitialHash, sizeof(hash));
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        uint32_t words[64] = {0};
        for (int i = 0; i < 16; ++i) {
            const std::size_t pos = offset + static_cast<std::size_t>(i) * 4;
            words[i] = (static_cast<uint32_t>(message[pos]) << 24) |
                       (static_cast<uint32_t>(message[pos + 1]) << 16) |
                       (static_cast<uint32_t>(message[pos + 2]) << 8) |
                       static_cast<uint32_t>(message[pos + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = RotateRight(words[i - 15], 7) ^
                                RotateRight(words[i - 15], 18) ^
                                (words[i - 15] >> 3);
            const uint32_t s1 = RotateRight(words[i - 2], 17) ^
                                RotateRight(words[i - 2], 19) ^
                                (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = hash[0];
        uint32_t b = hash[1];
        uint32_t c = hash[2];
        uint32_t d = hash[3];
        uint32_t e = hash[4];
        uint32_t f = hash[5];
        uint32_t g = hash[6];
        uint32_t h = hash[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^
                                RotateRight(e, 25);
            const uint32_t choose = (e & f) ^ ((~e) & g);
            const uint32_t temp1 =
                h + s1 + choose + kRoundConstants[i] + words[i];
            const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^
                                RotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    std::array<uint8_t, 32> digest = {};
    for (int i = 0; i < 8; ++i) {
        digest[static_cast<std::size_t>(i) * 4] =
            static_cast<uint8_t>((hash[i] >> 24) & 0xFF);
        digest[static_cast<std::size_t>(i) * 4 + 1] =
            static_cast<uint8_t>((hash[i] >> 16) & 0xFF);
        digest[static_cast<std::size_t>(i) * 4 + 2] =
            static_cast<uint8_t>((hash[i] >> 8) & 0xFF);
        digest[static_cast<std::size_t>(i) * 4 + 3] =
            static_cast<uint8_t>(hash[i] & 0xFF);
    }
    return digest;
}

}  // namespace

bool IsHexString(const std::string& value) {
    if (value.empty() || value.size() % 2 != 0) {
        return false;
    }
    for (char c : value) {
        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
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
    const std::array<uint8_t, 32> digest = Sha256(salt + password);
    return "sha256:" + salt_hex + ":" + BytesToHex(digest.data(), digest.size());
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
