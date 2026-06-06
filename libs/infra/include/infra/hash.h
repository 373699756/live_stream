#ifndef LIVE_STREAM_INFRA_HASH_H_
#define LIVE_STREAM_INFRA_HASH_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace infra {

class Sha256 {
public:
    Sha256();

    void Update(const void* data, std::size_t size);
    std::string FinishHex();

private:
    void ProcessBlock(const uint8_t* block);

    uint32_t hash_[8];
    uint8_t buffer_[64];
    uint64_t total_size_ = 0;
    std::size_t buffer_size_ = 0;
    bool finished_ = false;
};

std::string BytesToHex(const uint8_t* data, std::size_t size);
bool IsHexString(const std::string& value);
bool IsSha256HexString(const std::string& value);
std::string Sha256Hex(const std::string& data);
std::string Sha256FileHex(const std::string& path);

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_HASH_H_
