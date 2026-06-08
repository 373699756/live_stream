#include "media_codec.h"

#include "byte_writer.h"

#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace media_codec {
namespace {

void AppendHvccArray(std::string *record,
                     uint8_t nal_type,
                     const std::string &nal_unit) {
    if (nal_unit.empty()) {
        return;
    }
    byte_writer::AppendU8(record, static_cast<uint8_t>(0x80 |
                                                       (nal_type & 0x3f)));
    byte_writer::AppendU16(record, 1);
    byte_writer::AppendU16(record, static_cast<uint16_t>(nal_unit.size()));
    record->append(nal_unit);
}

}  // namespace

bool WriteNalLengthPrefix(size_t nal_size, uint8_t *out) {
    if (out == nullptr || nal_size > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    out[0] = static_cast<uint8_t>((nal_size >> 24) & 0xff);
    out[1] = static_cast<uint8_t>((nal_size >> 16) & 0xff);
    out[2] = static_cast<uint8_t>((nal_size >> 8) & 0xff);
    out[3] = static_cast<uint8_t>(nal_size & 0xff);
    return true;
}

bool AppendLengthPrefixedNal(const uint8_t *data,
                             size_t size,
                             std::string *out) {
    if (data == nullptr || size == 0 || out == nullptr) {
        return false;
    }
    uint8_t length[4] = {};
    if (!WriteNalLengthPrefix(size, length)) {
        return false;
    }
    out->append(reinterpret_cast<const char *>(length), sizeof(length));
    out->append(reinterpret_cast<const char *>(data), size);
    return true;
}

bool BuildH264AvccRecord(const std::string &sps,
                         const std::string &pps,
                         std::string *record) {
    if (record == nullptr || sps.empty() || pps.empty() ||
        sps.size() > std::numeric_limits<uint16_t>::max() ||
        pps.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    record->clear();
    byte_writer::AppendU8(record, 1);
    byte_writer::AppendU8(record,
                          sps.size() > 1 ? static_cast<uint8_t>(sps[1])
                                         : 0x64);
    byte_writer::AppendU8(record,
                          sps.size() > 2 ? static_cast<uint8_t>(sps[2])
                                         : 0x00);
    byte_writer::AppendU8(record,
                          sps.size() > 3 ? static_cast<uint8_t>(sps[3])
                                         : 0x1f);
    byte_writer::AppendU8(record, 0xff);
    byte_writer::AppendU8(record, 0xe1);
    byte_writer::AppendU16(record, static_cast<uint16_t>(sps.size()));
    record->append(sps);
    byte_writer::AppendU8(record, 1);
    byte_writer::AppendU16(record, static_cast<uint16_t>(pps.size()));
    record->append(pps);
    return true;
}

bool BuildH265HvccRecord(const std::string &vps,
                         const std::string &sps,
                         const std::string &pps,
                         std::string *record) {
    if (record == nullptr || vps.empty() || sps.empty() || pps.empty() ||
        vps.size() > std::numeric_limits<uint16_t>::max() ||
        sps.size() > std::numeric_limits<uint16_t>::max() ||
        pps.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    record->clear();
    byte_writer::AppendU8(record, 1);
    if (sps.size() >= 15) {
        byte_writer::AppendU8(record, static_cast<uint8_t>(sps[3]));
        record->append(sps.data() + 4, 4);
        record->append(sps.data() + 8, 6);
        byte_writer::AppendU8(record, static_cast<uint8_t>(sps[14]));
    } else {
        byte_writer::AppendU8(record, 0x01);
        byte_writer::AppendU32(record, 0x60000000);
        static constexpr uint8_t kConstraintFlags[] = {
            0x90, 0x00, 0x00, 0x00, 0x00, 0x00};
        record->append(reinterpret_cast<const char *>(kConstraintFlags),
                       sizeof(kConstraintFlags));
        byte_writer::AppendU8(record, 0x1e);
    }
    byte_writer::AppendU16(record, 0xf000);
    byte_writer::AppendU8(record, 0xfc);
    byte_writer::AppendU8(record, 0xfd);
    byte_writer::AppendU8(record, 0xf8);
    byte_writer::AppendU8(record, 0xf8);
    byte_writer::AppendU16(record, 0);
    byte_writer::AppendU8(record, 0x0f);

    byte_writer::AppendU8(record, 3);
    AppendHvccArray(record, kH265NalTypeVps, vps);
    AppendHvccArray(record, kH265NalTypeSps, sps);
    AppendHvccArray(record, kH265NalTypePps, pps);
    return true;
}

}  // namespace media_codec
}  // namespace live_stream
