#include "media_mux.h"

#include "byte_writer.h"
#include "media_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace live_stream {
namespace media_mux {
namespace {

constexpr uint16_t kPatPid = 0x0000;
constexpr uint16_t kPmtPid = 0x1000;
constexpr uint16_t kVideoPid = 0x0100;
constexpr uint8_t kTsPacketSize = 188;
constexpr uint8_t kTsStreamTypeH264 = 0x1b;
constexpr uint8_t kTsStreamTypeH265 = 0x24;
constexpr size_t kMaxTsMediaSlices = 192;

using byte_writer::AppendU8;
using byte_writer::AppendU16;
using byte_writer::AppendU24;
using byte_writer::AppendU32;

uint8_t TsStreamType(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? kTsStreamTypeH265 : kTsStreamTypeH264;
}

void FillBytes(char *target, size_t size, uint8_t value) {
    if (target == nullptr || size == 0) {
        return;
    }
    std::memset(target, value, size);
}

bool AppendTsBytes(TsSegmentBuffer *out, const uint8_t *data, size_t size) {
    if (size == 0) {
        return true;
    }
    if (out == nullptr || data == nullptr || out->size > out->capacity ||
        size > out->capacity - out->size) {
        return false;
    }
    std::memcpy(out->data + out->size, data, size);
    out->size += size;
    return true;
}

bool AppendTsString(TsSegmentBuffer *out, const std::string &data) {
    return data.empty() ||
           AppendTsBytes(out, reinterpret_cast<const uint8_t *>(data.data()),
                         data.size());
}

uint32_t MpegCrc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x80000000U) != 0U) {
                crc = (crc << 1) ^ 0x04c11db7U;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void AppendPsiSectionLength(std::string *out, uint16_t section_length) {
    AppendU8(out, static_cast<uint8_t>(0xb0 | ((section_length >> 8) & 0x0f)));
    AppendU8(out, static_cast<uint8_t>(section_length & 0xff));
}

void AppendPsiPid(std::string *out, uint16_t pid) {
    AppendU8(out, static_cast<uint8_t>(0xe0 | ((pid >> 8) & 0x1f)));
    AppendU8(out, static_cast<uint8_t>(pid & 0xff));
}

std::string BuildPatPacket(uint8_t *continuity_counter) {
    std::string section;
    AppendU8(&section, 0x00);
    AppendPsiSectionLength(&section, 13);
    AppendU16(&section, 1);
    AppendU8(&section, 0xc1);
    AppendU8(&section, 0);
    AppendU8(&section, 0);
    AppendU16(&section, 1);
    AppendPsiPid(&section, kPmtPid);
    const uint32_t crc = MpegCrc32(
        reinterpret_cast<const uint8_t *>(section.data()), section.size());
    AppendU32(&section, crc);

    std::string packet(kTsPacketSize, '\0');
    FillBytes(&packet[0], packet.size(), 0xff);
    packet[0] = static_cast<char>(0x47);
    packet[1] = static_cast<char>(0x40 | ((kPatPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kPatPid & 0xff);
    packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
    *continuity_counter =
        static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
    packet[4] = static_cast<char>(0);
    std::copy(section.begin(), section.end(), packet.begin() + 5);
    return packet;
}

std::string BuildPmtPacket(VideoCodec codec, uint8_t *continuity_counter) {
    std::string section;
    AppendU8(&section, 0x02);
    AppendPsiSectionLength(&section, 18);
    AppendU16(&section, 1);
    AppendU8(&section, 0xc1);
    AppendU8(&section, 0);
    AppendU8(&section, 0);
    AppendPsiPid(&section, kVideoPid);
    AppendU16(&section, 0xf000);
    AppendU8(&section, TsStreamType(codec));
    AppendPsiPid(&section, kVideoPid);
    AppendU16(&section, 0xf000);
    const uint32_t crc = MpegCrc32(
        reinterpret_cast<const uint8_t *>(section.data()), section.size());
    AppendU32(&section, crc);

    std::string packet(kTsPacketSize, '\0');
    FillBytes(&packet[0], packet.size(), 0xff);
    packet[0] = static_cast<char>(0x47);
    packet[1] = static_cast<char>(0x40 | ((kPmtPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kPmtPid & 0xff);
    packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
    *continuity_counter =
        static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
    packet[4] = static_cast<char>(0);
    std::copy(section.begin(), section.end(), packet.begin() + 5);
    return packet;
}

void AppendPts(std::string *out, uint8_t prefix, uint64_t value) {
    const uint64_t pts = value & 0x1ffffffffULL;
    AppendU8(out, static_cast<uint8_t>(
                      (prefix << 4) | (((pts >> 30) & 0x07) << 1) | 0x01));
    AppendU8(out, static_cast<uint8_t>((pts >> 22) & 0xff));
    AppendU8(out,
             static_cast<uint8_t>((((pts >> 15) & 0x7f) << 1) | 0x01));
    AppendU8(out, static_cast<uint8_t>((pts >> 7) & 0xff));
    AppendU8(out, static_cast<uint8_t>(((pts & 0x7f) << 1) | 0x01));
}

std::string BuildPesHeader(uint64_t pts_90k, uint64_t dts_90k) {
    std::string header;
    AppendU24(&header, 0x000001);
    AppendU8(&header, 0xe0);
    AppendU16(&header, 0);
    AppendU8(&header, 0x80);
    const bool has_dts = pts_90k != dts_90k;
    AppendU8(&header, has_dts ? 0xc0 : 0x80);
    AppendU8(&header, has_dts ? 10 : 5);
    AppendPts(&header, has_dts ? 0x03 : 0x02, pts_90k);
    if (has_dts) {
        AppendPts(&header, 0x01, dts_90k);
    }
    return header;
}

void WritePcr(char *target, uint64_t pcr_90k) {
    const uint64_t base = pcr_90k & 0x1ffffffffULL;
    target[0] = static_cast<char>((base >> 25) & 0xff);
    target[1] = static_cast<char>((base >> 17) & 0xff);
    target[2] = static_cast<char>((base >> 9) & 0xff);
    target[3] = static_cast<char>((base >> 1) & 0xff);
    target[4] = static_cast<char>(((base & 0x01) << 7) | 0x7e);
    target[5] = static_cast<char>(0);
}

struct MediaSliceList {
    struct Slice {
        const uint8_t *data = nullptr;
        size_t size = 0;
    };

    Slice slices[kMaxTsMediaSlices];
    size_t count = 0;
    size_t total_size = 0;

    bool Add(const uint8_t *data, size_t size) {
        if (size == 0) {
            return true;
        }
        if (data == nullptr || count >= kMaxTsMediaSlices ||
            size > std::numeric_limits<size_t>::max() - total_size) {
            return false;
        }
        slices[count].data = data;
        slices[count].size = size;
        total_size += size;
        ++count;
        return true;
    }

    bool AddString(const std::string &data) {
        return data.empty() ||
               Add(reinterpret_cast<const uint8_t *>(data.data()),
                   data.size());
    }
};

size_t CopyPesBytes(const MediaSliceList &pes_slices,
                    size_t offset,
                    size_t size,
                    char *target) {
    if (target == nullptr || size == 0) {
        return 0;
    }
    size_t copied = 0;
    size_t slice_base = 0;
    for (size_t i = 0; i < pes_slices.count && copied < size; ++i) {
        const MediaSliceList::Slice &slice = pes_slices.slices[i];
        if (offset >= slice_base + slice.size) {
            slice_base += slice.size;
            continue;
        }
        const size_t slice_offset =
            offset > slice_base ? offset - slice_base : 0;
        const size_t copy_size =
            std::min(size - copied, slice.size - slice_offset);
        std::memcpy(target + copied, slice.data + slice_offset, copy_size);
        copied += copy_size;
        offset += copy_size;
        slice_base += slice.size;
    }
    return copied;
}

bool AppendTsPayloadToBuffer(const MediaSliceList &pes_slices,
                             uint64_t pcr_90k,
                             uint8_t *continuity_counter,
                             TsSegmentBuffer *out) {
    if (out == nullptr || continuity_counter == nullptr) {
        return false;
    }
    const size_t pes_size = pes_slices.total_size;
    const size_t packet_count = pes_size == 0 ? 0 : (pes_size + 175) / 176;
    if (out->size > out->capacity) {
        return false;
    }
    if (packet_count > (out->capacity - out->size) / kTsPacketSize) {
        return false;
    }

    size_t offset = 0;
    while (offset < pes_size) {
        const bool first_packet = offset == 0;
        const size_t remaining = pes_size - offset;
        size_t payload_size = 0;
        if (first_packet) {
            payload_size = std::min(remaining, static_cast<size_t>(176));
        } else {
            payload_size = std::min(remaining, static_cast<size_t>(184));
            if (payload_size == 183) {
                --payload_size;
            }
        }
        const bool use_adaptation = first_packet || payload_size < 184;
        if (use_adaptation && payload_size == 183) {
            --payload_size;
        }
        const size_t adaptation_total = use_adaptation ? 184 - payload_size : 0;

        char packet[kTsPacketSize];
        FillBytes(packet, kTsPacketSize, 0xff);
        packet[0] = static_cast<char>(0x47);
        packet[1] = static_cast<char>((first_packet ? 0x40 : 0x00) |
                                      ((kVideoPid >> 8) & 0x1f));
        packet[2] = static_cast<char>(kVideoPid & 0xff);
        packet[3] = static_cast<char>((use_adaptation ? 0x30 : 0x10) |
                                      (*continuity_counter & 0x0f));
        *continuity_counter =
            static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);

        size_t packet_offset = 4;
        if (use_adaptation) {
            const size_t adaptation_length = adaptation_total - 1;
            packet[packet_offset++] = static_cast<char>(adaptation_length);
            packet[packet_offset++] = first_packet ? static_cast<char>(0x10)
                                                   : static_cast<char>(0);
            size_t stuffing = adaptation_length - 1;
            if (first_packet) {
                WritePcr(&packet[packet_offset], pcr_90k);
                packet_offset += 6;
                stuffing -= 6;
            }
            for (size_t i = 0; i < stuffing; ++i) {
                packet[packet_offset++] = static_cast<char>(0xff);
            }
        }
        (void)CopyPesBytes(pes_slices, offset, payload_size,
                           packet + packet_offset);
        if (!AppendTsBytes(out, reinterpret_cast<const uint8_t *>(packet),
                           sizeof(packet))) {
            return false;
        }
        offset += payload_size;
    }
    return true;
}

bool AddH264AccessUnitSlices(const media_codec::H264NalUnitList &units,
                             const std::string &sps,
                             const std::string &pps,
                             bool prepend_parameter_sets,
                             MediaSliceList *slices) {
    static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    static constexpr uint8_t kAud[] = {0x09, 0xf0};
    if (slices == nullptr) {
        return false;
    }
    if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
        !slices->Add(kAud, sizeof(kAud))) {
        return false;
    }
    if (prepend_parameter_sets && !sps.empty() && !pps.empty()) {
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(sps.data()),
                         sps.size()) ||
            !slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(pps.data()),
                         pps.size())) {
            return false;
        }
    }
    for (const media_codec::H264NalUnit &unit : units) {
        if (unit.type == media_codec::kH264NalTypeAud) {
            continue;
        }
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(unit.data, unit.size)) {
            return false;
        }
    }
    return true;
}

bool AddH265AccessUnitSlices(const media_codec::H265NalUnitList &units,
                             const std::string &vps,
                             const std::string &sps,
                             const std::string &pps,
                             bool prepend_parameter_sets,
                             MediaSliceList *slices) {
    static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    static constexpr uint8_t kAud[] = {0x46, 0x01, 0x50};
    if (slices == nullptr) {
        return false;
    }
    if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
        !slices->Add(kAud, sizeof(kAud))) {
        return false;
    }
    if (prepend_parameter_sets && !vps.empty() && !sps.empty() && !pps.empty()) {
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(vps.data()),
                         vps.size()) ||
            !slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(sps.data()),
                         sps.size()) ||
            !slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(reinterpret_cast<const uint8_t *>(pps.data()),
                         pps.size())) {
            return false;
        }
    }
    for (const media_codec::H265NalUnit &unit : units) {
        if (unit.type == media_codec::kH265NalTypeAud) {
            continue;
        }
        if (!slices->Add(kStartCode, sizeof(kStartCode)) ||
            !slices->Add(unit.data, unit.size)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool AppendTsSegmentHeader(VideoCodec codec,
                           TsMuxerState *state,
                           TsSegmentBuffer *segment_body) {
    if (state == nullptr || segment_body == nullptr ||
        segment_body->size > segment_body->capacity ||
        segment_body->capacity - segment_body->size < kTsPacketSize * 2U) {
        return false;
    }
    const std::string pat = BuildPatPacket(&state->pat_continuity);
    const std::string pmt = BuildPmtPacket(codec, &state->pmt_continuity);
    return AppendTsString(segment_body, pat) &&
           AppendTsString(segment_body, pmt);
}

bool AppendH264NalUnitsToTsSegmentBuffer(
    const media_codec::H264NalUnitList &units,
    const std::string &sps,
    const std::string &pps,
    bool prepend_parameter_sets,
    int64_t pts_us,
    int64_t dts_us,
    TsMuxerState *state,
    TsSegmentBuffer *segment_body) {
    if (units.empty() || state == nullptr || segment_body == nullptr) {
        return false;
    }
    const uint64_t pts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, pts_us) * 9 / 100);
    const uint64_t dts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, dts_us) * 9 / 100);
    const std::string pes_header = BuildPesHeader(pts_90k, dts_90k);
    MediaSliceList pes_slices;
    if (!pes_slices.AddString(pes_header) ||
        !AddH264AccessUnitSlices(units, sps, pps, prepend_parameter_sets,
                                 &pes_slices)) {
        return false;
    }
    return AppendTsPayloadToBuffer(pes_slices, dts_90k,
                                   &state->video_continuity, segment_body);
}

bool AppendH265NalUnitsToTsSegmentBuffer(
    const media_codec::H265NalUnitList &units,
    const std::string &vps,
    const std::string &sps,
    const std::string &pps,
    bool prepend_parameter_sets,
    int64_t pts_us,
    int64_t dts_us,
    TsMuxerState *state,
    TsSegmentBuffer *segment_body) {
    if (units.empty() || state == nullptr || segment_body == nullptr) {
        return false;
    }
    const uint64_t pts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, pts_us) * 9 / 100);
    const uint64_t dts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, dts_us) * 9 / 100);
    const std::string pes_header = BuildPesHeader(pts_90k, dts_90k);
    MediaSliceList pes_slices;
    if (!pes_slices.AddString(pes_header) ||
        !AddH265AccessUnitSlices(units, vps, sps, pps, prepend_parameter_sets,
                                 &pes_slices)) {
        return false;
    }
    return AppendTsPayloadToBuffer(pes_slices, dts_90k,
                                   &state->video_continuity, segment_body);
}

}  // namespace media_mux
}  // namespace live_stream
