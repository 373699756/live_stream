#ifndef LIVE_STREAM_COMMON_BYTE_WRITER_H_
#define LIVE_STREAM_COMMON_BYTE_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {
namespace byte_writer {

inline void AppendU8(std::string *out, uint8_t value) {
    if (out != nullptr) {
        out->push_back(static_cast<char>(value));
    }
}

inline void AppendBytes(std::string *out, const uint8_t *data, size_t size) {
    if (out == nullptr || data == nullptr) {
        return;
    }
    out->append(reinterpret_cast<const char *>(data), size);
}

inline void AppendU16(std::string *out, uint16_t value) {
    AppendU8(out, static_cast<uint8_t>((value >> 8) & 0xff));
    AppendU8(out, static_cast<uint8_t>(value & 0xff));
}

inline void AppendU24(std::string *out, uint32_t value) {
    AppendU8(out, static_cast<uint8_t>((value >> 16) & 0xff));
    AppendU8(out, static_cast<uint8_t>((value >> 8) & 0xff));
    AppendU8(out, static_cast<uint8_t>(value & 0xff));
}

inline void AppendU32(std::string *out, uint32_t value) {
    AppendU8(out, static_cast<uint8_t>((value >> 24) & 0xff));
    AppendU8(out, static_cast<uint8_t>((value >> 16) & 0xff));
    AppendU8(out, static_cast<uint8_t>((value >> 8) & 0xff));
    AppendU8(out, static_cast<uint8_t>(value & 0xff));
}

inline void WriteU16(uint8_t *out, uint16_t value) {
    if (out == nullptr) {
        return;
    }
    out[0] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[1] = static_cast<uint8_t>(value & 0xff);
}

inline void WriteU32(uint8_t *out, uint32_t value) {
    if (out == nullptr) {
        return;
    }
    out[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    out[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[3] = static_cast<uint8_t>(value & 0xff);
}

}  // namespace byte_writer
}  // namespace live_stream

#endif  // LIVE_STREAM_COMMON_BYTE_WRITER_H_
