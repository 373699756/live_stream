#include "media_codec.h"

#include "byte_writer.h"

#include <cstdint>
#include <limits>
#include <cstdio>
#include <string>

namespace live_stream {
namespace media_codec {
namespace {

struct H265ProfileTierLevel {
    uint8_t profile_byte = 0x01;
    std::string compatibility_flags;
    std::string constraint_flags;
    uint8_t level_idc = 0x1e;
    uint8_t max_sub_layers_minus1 = 0;
    bool temporal_id_nested = true;
};

H265ProfileTierLevel DefaultH265MainProfile() {
    H265ProfileTierLevel profile;
    profile.profile_byte = 0x01;
    profile.compatibility_flags.assign(4, '\0');
    profile.compatibility_flags[0] = static_cast<char>(0x60);
    profile.constraint_flags.assign(6, '\0');
    profile.constraint_flags[0] = static_cast<char>(0xb0);
    profile.level_idc = 0x5d;
    profile.max_sub_layers_minus1 = 0;
    profile.temporal_id_nested = true;
    return profile;
}

std::string H265NalRbsp(const std::string &nal_unit) {
    std::string rbsp;
    if (nal_unit.size() <= 2) {
        return rbsp;
    }
    rbsp.reserve(nal_unit.size() - 2);
    uint8_t zero_count = 0;
    for (size_t i = 2; i < nal_unit.size(); ++i) {
        const uint8_t byte = static_cast<uint8_t>(nal_unit[i]);
        if (zero_count >= 2 && byte == 0x03) {
            zero_count = 0;
            continue;
        }
        rbsp.push_back(static_cast<char>(byte));
        if (byte == 0) {
            ++zero_count;
        } else {
            zero_count = 0;
        }
    }
    return rbsp;
}

bool ExtractH265ProfileFromVps(const std::string &vps,
                               H265ProfileTierLevel *profile) {
    if (profile == nullptr) {
        return false;
    }
    const std::string rbsp = H265NalRbsp(vps);
    if (rbsp.size() < 16) {
        return false;
    }
    profile->max_sub_layers_minus1 =
        static_cast<uint8_t>((static_cast<uint8_t>(rbsp[1]) >> 1) & 0x07);
    profile->temporal_id_nested =
        (static_cast<uint8_t>(rbsp[1]) & 0x01) != 0;
    profile->profile_byte = static_cast<uint8_t>(rbsp[4]);
    profile->compatibility_flags.assign(rbsp.data() + 5, 4);
    profile->constraint_flags.assign(rbsp.data() + 9, 6);
    profile->level_idc = static_cast<uint8_t>(rbsp[15]);
    return true;
}

void AppendH265ProfileTierLevel(const H265ProfileTierLevel &profile,
                                std::string *record) {
    byte_writer::AppendU8(record, profile.profile_byte);
    record->append(profile.compatibility_flags);
    record->append(profile.constraint_flags);
    byte_writer::AppendU8(record, profile.level_idc);
}

uint8_t H265HvccTemporalByte(const H265ProfileTierLevel &profile) {
    const uint8_t temporal_layers =
        static_cast<uint8_t>(profile.max_sub_layers_minus1 + 1U);
    return static_cast<uint8_t>(
        ((temporal_layers & 0x07) << 3) |
        (profile.temporal_id_nested ? 0x04 : 0x00) |
        0x03);
}

uint32_t ReadU32(const std::string &data) {
    if (data.size() < 4) {
        return 0;
    }
    return (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
}

uint32_t ReverseBits32(uint32_t value) {
    uint32_t reversed = 0;
    for (int i = 0; i < 32; ++i) {
        reversed |= ((value >> i) & 1U) << (31 - i);
    }
    return reversed;
}

std::string HexNoLeadingZero(uint32_t value) {
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%X", value);
    return text;
}

std::string H265CodecString(const H265ProfileTierLevel &profile) {
    const uint8_t profile_space =
        static_cast<uint8_t>((profile.profile_byte >> 6) & 0x03);
    const uint8_t profile_idc =
        static_cast<uint8_t>(profile.profile_byte & 0x1f);
    const bool tier_high = (profile.profile_byte & 0x20) != 0;
    std::string codec = "hvc1.";
    if (profile_space == 1) {
        codec += "A";
    } else if (profile_space == 2) {
        codec += "B";
    } else if (profile_space == 3) {
        codec += "C";
    }
    codec += std::to_string(profile_idc);
    codec += ".";
    codec += HexNoLeadingZero(
        ReverseBits32(ReadU32(profile.compatibility_flags)));
    codec += ".";
    codec += tier_high ? "H" : "L";
    codec += std::to_string(profile.level_idc);
    int last_constraint_index =
        static_cast<int>(profile.constraint_flags.size()) - 1;
    while (last_constraint_index >= 0 &&
           static_cast<uint8_t>(
               profile.constraint_flags[last_constraint_index]) == 0) {
        --last_constraint_index;
    }
    for (int i = 0; i <= last_constraint_index; ++i) {
        codec += ".";
        codec += HexNoLeadingZero(
            static_cast<uint8_t>(profile.constraint_flags[i]));
    }
    return codec;
}

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
    // FLV/AVCC/HVCC 中的视频 NAL 不使用 AnnexB 起始码，而使用
    // 4 字节 big-endian 长度前缀标出后续 NAL payload 大小。
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
    // avcC 记录里的 profile/compatibility/level 来自 SPS。异常短 SPS
    // 只在防御路径使用保守默认值，正常编码器都会给出完整 SPS。
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
                         std::string *record,
                         std::string *codec_string) {
    if (record == nullptr || vps.empty() || sps.empty() || pps.empty() ||
        vps.size() > std::numeric_limits<uint16_t>::max() ||
        sps.size() > std::numeric_limits<uint16_t>::max() ||
        pps.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    record->clear();
    byte_writer::AppendU8(record, 1);
    H265ProfileTierLevel profile = DefaultH265MainProfile();
    if (!ExtractH265ProfileFromVps(vps, &profile)) {
        profile = DefaultH265MainProfile();
    }
    AppendH265ProfileTierLevel(profile, record);
    if (codec_string != nullptr) {
        *codec_string = H265CodecString(profile);
    }
    byte_writer::AppendU16(record, 0xf000);
    byte_writer::AppendU8(record, 0xfc);
    byte_writer::AppendU8(record, 0xfd);
    byte_writer::AppendU8(record, 0xf8);
    byte_writer::AppendU8(record, 0xf8);
    byte_writer::AppendU16(record, 0);
    byte_writer::AppendU8(record, H265HvccTemporalByte(profile));

    byte_writer::AppendU8(record, 3);
    // 每个 array 只保存一条当前生效的参数集，调用方在 codec 切换或参数集
    // 更新时会重新生成 sequence header。
    AppendHvccArray(record, kH265NalTypeVps, vps);
    AppendHvccArray(record, kH265NalTypeSps, sps);
    AppendHvccArray(record, kH265NalTypePps, pps);
    return true;
}

}  // namespace media_codec
}  // namespace live_stream
