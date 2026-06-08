#include "infra/hash.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace infra {
namespace {

uint32_t RotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

uint32_t ReadBigEndian32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void WriteBigEndian64(uint64_t value, uint8_t* data) {
    for (int i = 7; i >= 0; --i) {
        data[static_cast<std::size_t>(7 - i)] =
            static_cast<uint8_t>((value >> (i * 8)) & 0xffU);
    }
}

}  // namespace

Sha256::Sha256() {
    static const uint32_t kInitialHash[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::memcpy(hash_, kInitialHash, sizeof(hash_));
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::Update(const void* data, std::size_t size) {
    if (finished_ || data == nullptr || size == 0) {
        return;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    total_size_ += static_cast<uint64_t>(size);
    while (size > 0) {
        const std::size_t free_size = sizeof(buffer_) - buffer_size_;
        const std::size_t copy_size = size < free_size ? size : free_size;
        std::memcpy(buffer_ + buffer_size_, bytes, copy_size);
        buffer_size_ += copy_size;
        bytes += copy_size;
        size -= copy_size;
        if (buffer_size_ == sizeof(buffer_)) {
            ProcessBlock(buffer_);
            buffer_size_ = 0;
        }
    }
}

bool Sha256::Finish(uint8_t* digest, std::size_t size) {
    if (digest == nullptr || size < 32) {
        return false;
    }
    if (!finished_) {
        const uint64_t bit_size = total_size_ * 8ULL;
        uint8_t padding[128] = {0};
        padding[0] = 0x80U;
        const std::size_t pad_size =
            buffer_size_ < 56 ? 56 - buffer_size_ : 120 - buffer_size_;
        Update(padding, pad_size);
        uint8_t length_bytes[8] = {0};
        WriteBigEndian64(bit_size, length_bytes);
        Update(length_bytes, sizeof(length_bytes));
        finished_ = true;
    }

    for (int i = 0; i < 8; ++i) {
        digest[static_cast<std::size_t>(i) * 4] =
            static_cast<uint8_t>((hash_[i] >> 24) & 0xffU);
        digest[static_cast<std::size_t>(i) * 4 + 1] =
            static_cast<uint8_t>((hash_[i] >> 16) & 0xffU);
        digest[static_cast<std::size_t>(i) * 4 + 2] =
            static_cast<uint8_t>((hash_[i] >> 8) & 0xffU);
        digest[static_cast<std::size_t>(i) * 4 + 3] =
            static_cast<uint8_t>(hash_[i] & 0xffU);
    }
    return true;
}

std::string Sha256::FinishHex() {
    uint8_t digest[32] = {0};
    if (!Finish(digest, sizeof(digest))) {
        return std::string();
    }
    return BytesToHex(digest, sizeof(digest));
}

void Sha256::ProcessBlock(const uint8_t* block) {
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

    uint32_t words[64] = {0};
    for (int i = 0; i < 16; ++i) {
        words[i] = ReadBigEndian32(block + static_cast<std::size_t>(i) * 4);
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

    uint32_t a = hash_[0];
    uint32_t b = hash_[1];
    uint32_t c = hash_[2];
    uint32_t d = hash_[3];
    uint32_t e = hash_[4];
    uint32_t f = hash_[5];
    uint32_t g = hash_[6];
    uint32_t h = hash_[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 =
            RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 =
            h + s1 + choose + kRoundConstants[i] + words[i];
        const uint32_t s0 =
            RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
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
    hash_[0] += a;
    hash_[1] += b;
    hash_[2] += c;
    hash_[3] += d;
    hash_[4] += e;
    hash_[5] += f;
    hash_[6] += g;
    hash_[7] += h;
}

std::string BytesToHex(const uint8_t* data, std::size_t size) {
    if (data == nullptr && size > 0) {
        return std::string();
    }
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    hex.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        hex[i * 2] = kHex[(data[i] >> 4) & 0x0fU];
        hex[i * 2 + 1] = kHex[data[i] & 0x0fU];
    }
    return hex;
}

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

bool IsSha256HexString(const std::string& value) {
    return value.size() == 64 && IsHexString(value);
}

std::string Sha256Hex(const std::string& data) {
    Sha256 sha;
    sha.Update(data.data(), data.size());
    return sha.FinishHex();
}

std::string Sha256FileHex(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::string();
    }

    Sha256 sha;
    uint8_t buffer[64 * 1024];
    while (true) {
        const std::size_t read_size = std::fread(buffer, 1, sizeof(buffer), file);
        if (read_size > 0) {
            sha.Update(buffer, read_size);
        }
        if (read_size < sizeof(buffer)) {
            break;
        }
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok ? sha.FinishHex() : std::string();
}

}  // namespace infra
