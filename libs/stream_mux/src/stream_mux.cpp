#include "stream_mux.h"

#include "live_stream/byte_writer.h"
#include "stream_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace live_stream {
namespace stream_mux {
namespace {

// Protocol notes for this muxer:
//
// Wire-format conventions:
// - Multi-byte integers are written in network byte order. Use AppendU16,
//   AppendU24, and AppendU32 for fixed-width protocol fields instead of raw
//   escaped byte strings.
// - Short signatures such as "FLV" and FourCC values such as "hvc1" stay as
//   string literals because they are textual identifiers, not numeric fields.
//
// RTP:
// - Input video frames may contain one Annex-B access unit: start code + NAL,
//   start code + NAL... Packetize() strips those start codes and sends each
//   NAL as either one RTP packet or a sequence of RTP fragments.
// - The RTP header is 12 bytes. Fragmentation overhead is 2 bytes for H.264
//   FU-A and 3 bytes for H.265 FU, so the per-packet payload budget is MTU
//   minus those totals.
// - H.264 FU-A uses NAL type 28. The FU indicator keeps the original
//   forbidden_zero_bit and nal_ref_idc bits; the FU header carries start/end
//   flags and the original NAL type.
// - H.265 FU uses NAL type 49. It keeps the original layer/temporal bits in
//   the two-byte payload header; the FU header carries start/end flags and the
//   original NAL type.
// - The RTP marker bit is set only on the last packet of an access unit, not
//   on every fragmented NAL.
//
// MPEG-TS/HLS:
// - TS packets are fixed 188 bytes: 4 bytes of TS header plus up to 184 bytes
//   of payload or adaptation field.
// - Each HLS segment starts with PAT and PMT so the segment can be decoded
//   independently. This muxer uses PID 0x0000 for PAT, 0x1000 for PMT, and
//   0x0100 for video.
// - PMT stream type 0x1b means H.264/AVC and 0x24 means H.265/HEVC.
// - Video access units are wrapped in PES first, then split into TS packets.
// - PES timestamps are 90 kHz PTS/DTS values. Video PES packet length is set
//   to 0 because live video PES payloads may exceed 64 KiB.
// - The first TS packet for a PES carries PCR in the adaptation field so a
//   player can recover the sender clock. PCR uses the same 90 kHz base here.
//
// FLV/HTTP-FLV:
// - HTTP-FLV sends a FLV header, then a codec sequence header, then video tags.
// - Each FLV tag has an 11-byte tag header, a 3-byte StreamID that is always 0,
//   the codec-specific video payload, and a 4-byte PreviousTagSize.
// - H.264 uses the legacy FLV AVC tag format. Its sequence header contains
//   AVCDecoderConfigurationRecord (avcC); video frames are length-prefixed NALs.
// - H.265 uses Enhanced FLV. Tags carry FourCC "hvc1"; the sequence header
//   contains HEVCDecoderConfigurationRecord (hvcC); video frames are
//   length-prefixed NALs.

constexpr uint8_t kPayloadTypeH264 = 96;
constexpr uint8_t kPayloadTypeH265 = 98;
constexpr uint32_t kRtpClockRate = 90000;
constexpr uint32_t kRtpHeaderSize = 12;
constexpr uint32_t kH264FuOverhead = kRtpHeaderSize + 2;
constexpr uint32_t kH265FuOverhead = kRtpHeaderSize + 3;
constexpr uint8_t kH264FuANalType = 28;
constexpr uint8_t kH265FuNalType = 49;
constexpr uint16_t kPatPid = 0x0000;
constexpr uint16_t kPmtPid = 0x1000;
constexpr uint16_t kVideoPid = 0x0100;
constexpr uint8_t kTsPacketSize = 188;
constexpr uint8_t kTsStreamTypeH264 = 0x1b;
constexpr uint8_t kTsStreamTypeH265 = 0x24;
constexpr uint8_t kFlvCodecIdAvc = 7;
constexpr uint8_t kEnhancedFlvHeader = 0x80;
constexpr uint8_t kFlvFrameKey = 1 << 4;
constexpr uint8_t kFlvFrameInter = 2 << 4;
constexpr uint8_t kFlvPacketTypeSequenceStart = 0;
constexpr uint8_t kFlvPacketTypeCodedFrames = 1;
constexpr uint8_t kH265VpsNalType = 32;
constexpr uint8_t kH265SpsNalType = 33;
constexpr uint8_t kH265PpsNalType = 34;

using byte_writer::AppendBytes;
using byte_writer::AppendU8;
using byte_writer::AppendU16;
using byte_writer::AppendU24;
using byte_writer::AppendU32;
using byte_writer::WriteU16;
using byte_writer::WriteU32;

uint8_t PayloadType(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? kPayloadTypeH265 : kPayloadTypeH264;
}

uint8_t TsStreamType(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? kTsStreamTypeH265 : kTsStreamTypeH264;
}

uint32_t RtpTimestamp(const EncodedFrame &frame) {
    return static_cast<uint32_t>((frame.pts_us * kRtpClockRate) / 1000000);
}

void AppendFlvTimestamp(std::string *out, uint32_t timestamp_ms) {
    AppendU24(out, timestamp_ms & 0x00ffffffU);
    AppendU8(out, static_cast<uint8_t>((timestamp_ms >> 24) & 0xff));
}

void AppendFourCc(std::string *out, const char *fourcc) {
    out->append(fourcc, 4);
}

void WriteRtpHeader(const EncodedFrame &frame, bool marker, uint16_t sequence,
                    uint32_t ssrc, uint8_t *header) {
    header[0] = 0x80;
    header[1] =
        static_cast<uint8_t>((marker ? 0x80 : 0x00) | PayloadType(frame.codec));
    WriteU16(header + 2, sequence);
    WriteU32(header + 4, RtpTimestamp(frame));
    WriteU32(header + 8, ssrc);
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
    // PAT (Program Association Table): tells the player which PID carries PMT.
    // HLS TS segments need PAT/PMT at the start so every segment is decodable.
    std::string section;
    AppendU8(&section, 0x00);  // PAT table_id.
    AppendPsiSectionLength(&section, 13);
    AppendU16(&section, 1);      // transport_stream_id.
    AppendU8(&section, 0xc1);  // current_next_indicator.
    AppendU8(&section, 0);     // section_number.
    AppendU8(&section, 0);     // last_section_number.
    AppendU16(&section, 1);      // program_number.
    AppendPsiPid(&section, kPmtPid);
    const uint32_t crc = MpegCrc32(
        reinterpret_cast<const uint8_t *>(section.data()), section.size());
    AppendU32(&section, crc);

    std::string packet(kTsPacketSize, static_cast<char>(0xff));
    packet[0] = static_cast<char>(0x47);  // TS sync byte.
    packet[1] = static_cast<char>(0x40 | ((kPatPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kPatPid & 0xff);
    packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
    *continuity_counter = static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
    packet[4] = static_cast<char>(0);  // Section starts immediately.
    std::copy(section.begin(), section.end(), packet.begin() + 5);
    return packet;
}

std::string BuildPmtPacket(VideoCodec codec, uint8_t *continuity_counter) {
    // PMT (Program Map Table): declares the video PID and codec stream type.
    // 0x1b means H.264/AVC, 0x24 means H.265/HEVC.
    std::string section;
    AppendU8(&section, 0x02);  // PMT table_id.
    AppendPsiSectionLength(&section, 18);
    AppendU16(&section, 1);             // program_number.
    AppendU8(&section, 0xc1);         // current_next_indicator.
    AppendU8(&section, 0);            // section_number.
    AppendU8(&section, 0);            // last_section_number.
    AppendPsiPid(&section, kVideoPid);  // PCR PID.
    AppendU16(&section, 0xf000);        // program_info_length: no descriptors.
    AppendU8(&section, TsStreamType(codec));
    AppendPsiPid(&section, kVideoPid);
    AppendU16(&section, 0xf000);  // ES_info_length: no descriptors.
    const uint32_t crc = MpegCrc32(
        reinterpret_cast<const uint8_t *>(section.data()), section.size());
    AppendU32(&section, crc);

    std::string packet(kTsPacketSize, static_cast<char>(0xff));
    packet[0] = static_cast<char>(0x47);  // TS sync byte.
    packet[1] = static_cast<char>(0x40 | ((kPmtPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kPmtPid & 0xff);
    packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
    *continuity_counter = static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
    packet[4] = static_cast<char>(0);  // Section starts immediately.
    std::copy(section.begin(), section.end(), packet.begin() + 5);
    return packet;
}

void AppendPts(std::string *out, uint8_t prefix, uint64_t value) {
    // PTS/DTS are 33-bit 90 kHz timestamps split across five marker-bit bytes.
    const uint64_t pts = value & 0x1ffffffffULL;
    AppendU8(out, static_cast<uint8_t>(
                        (prefix << 4) | (((pts >> 30) & 0x07) << 1) | 0x01));
    AppendU8(out, static_cast<uint8_t>((pts >> 22) & 0xff));
    AppendU8(out,
               static_cast<uint8_t>((((pts >> 15) & 0x7f) << 1) | 0x01));
    AppendU8(out, static_cast<uint8_t>((pts >> 7) & 0xff));
    AppendU8(out, static_cast<uint8_t>(((pts & 0x7f) << 1) | 0x01));
}

std::string BuildPesPacket(const std::string &access_unit, uint64_t pts_90k,
                           uint64_t dts_90k) {
    // PES wraps one video access unit before it is split into 188-byte TS
    // packets. Packet length is set to 0 because live video PES can exceed 64 KiB.
    std::string pes;
    AppendU24(&pes, 0x000001);  // PES start code prefix.
    AppendU8(&pes, 0xe0);     // Video stream id.
    AppendU16(&pes, 0);         // Unbounded video PES length.
    AppendU8(&pes, 0x80);     // Marker bits.
    const bool has_dts = pts_90k != dts_90k;
    AppendU8(&pes, has_dts ? 0xc0 : 0x80);  // PTS+DTS or PTS only.
    AppendU8(&pes, has_dts ? 10 : 5);       // Optional header length.
    AppendPts(&pes, has_dts ? 0x03 : 0x02, pts_90k);
    if (has_dts) {
        AppendPts(&pes, 0x01, dts_90k);
    }
    pes.append(access_unit);
    return pes;
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

void AppendTsPayload(const std::string &pes, uint64_t pcr_90k,
                     uint8_t *continuity_counter, std::string *out) {
    // Split PES into fixed 188-byte TS packets. The first packet carries PCR in
    // the adaptation field so HLS players can recover the sender clock.
    size_t offset = 0;
    while (offset < pes.size()) {
        const bool first_packet = offset == 0;
        const size_t remaining = pes.size() - offset;
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

        std::string packet(kTsPacketSize, static_cast<char>(0xff));
        packet[0] = static_cast<char>(0x47);  // TS sync byte.
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
        std::copy(pes.begin() + static_cast<std::ptrdiff_t>(offset),
                  pes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size),
                  packet.begin() + static_cast<std::ptrdiff_t>(packet_offset));
        out->append(packet);
        offset += payload_size;
    }
}

std::string BuildH264FlvConfigurationRecord(const std::string &sps,
                                            const std::string &pps) {
    // AVCDecoderConfigurationRecord (avcC) carried by the FLV sequence header:
    // version, profile/compatibility/level from SPS, NAL length size, SPS, PPS.
    std::string config;
    AppendU8(&config, 1);  // configurationVersion.
    AppendU8(&config, sps.size() > 1 ? static_cast<uint8_t>(sps[1]) : 0x64);
    AppendU8(&config, sps.size() > 2 ? static_cast<uint8_t>(sps[2]) : 0x00);
    AppendU8(&config, sps.size() > 3 ? static_cast<uint8_t>(sps[3]) : 0x1f);
    AppendU8(&config, 0xff);  // 4-byte NAL length prefix.
    AppendU8(&config, 0xe1);  // One SPS.
    AppendU16(&config, static_cast<uint16_t>(sps.size()));
    config.append(sps);
    AppendU8(&config, 1);
    AppendU16(&config, static_cast<uint16_t>(pps.size()));
    config.append(pps);
    return config;
}

void AppendHvccArray(std::string *config, uint8_t nal_type,
                     const std::string &nal_unit) {
    if (nal_unit.empty()) {
        return;
    }
    AppendU8(config, static_cast<uint8_t>(0x80 | (nal_type & 0x3f)));
    AppendU16(config, 1);
    AppendU16(config, static_cast<uint16_t>(nal_unit.size()));
    config->append(nal_unit);
}

std::string BuildH265HvccRecord(const std::string &vps,
                                const std::string &sps,
                                const std::string &pps) {
    // HEVCDecoderConfigurationRecord (hvcC): profile/tier/level from SPS when
    // available, followed by VPS/SPS/PPS arrays. Enhanced FLV uses this as the
    // sequence-start payload for H.265.
    // hvcC 是 H.265 播放器的“解码说明书”；后续视频 tag 只带长度前缀 NAL，
    // 因此这里必须缓存并写出 VPS/SPS/PPS。
    std::string config;
    AppendU8(&config, 1);  // configurationVersion.
    if (sps.size() >= 15) {
        AppendU8(&config, static_cast<uint8_t>(sps[3]));
        config.append(sps.data() + 4, 4);
        config.append(sps.data() + 8, 6);
        AppendU8(&config, static_cast<uint8_t>(sps[14]));
    } else {
        AppendU8(&config, 0x01);       // general_profile_space/tier/profile_idc.
        AppendU32(&config, 0x60000000);  // profile_compatibility_flags.
        static constexpr uint8_t kConstraintFlags[] = {
            0x90, 0x00, 0x00, 0x00, 0x00, 0x00};
        AppendBytes(&config, kConstraintFlags, sizeof(kConstraintFlags));
        AppendU8(&config, 0x1e);  // general_level_idc fallback.
    }
    AppendU16(&config, 0xf000);  // min_spatial_segmentation_idc unknown.
    AppendU8(&config, 0xfc);   // parallelismType unknown.
    AppendU8(&config, 0xfd);   // chromaFormat 4:2:0.
    AppendU8(&config, 0xf8);   // bitDepthLumaMinus8 = 0.
    AppendU8(&config, 0xf8);   // bitDepthChromaMinus8 = 0.
    AppendU16(&config, 0);       // avgFrameRate unknown.
    AppendU8(&config, 0x0f);   // constantFrameRate + temporal layers + length.

    uint8_t array_count = 0;
    if (!vps.empty()) {
        ++array_count;
    }
    if (!sps.empty()) {
        ++array_count;
    }
    if (!pps.empty()) {
        ++array_count;
    }
    AppendU8(&config, array_count);
    AppendHvccArray(&config, kH265VpsNalType, vps);
    AppendHvccArray(&config, kH265SpsNalType, sps);
    AppendHvccArray(&config, kH265PpsNalType, pps);
    return config;
}

std::string BuildH264FlvVideoTag(bool keyframe, uint8_t avc_packet_type,
                                 int32_t composition_time_ms,
                                 uint32_t timestamp_ms,
                                 const std::string &payload) {
    // FLV video tag layout:
    // tag header, 3-byte StreamID (always 0), video flags, packet type,
    // composition time, then codec payload.
    std::string tag;
    const uint32_t body_size = 5U + static_cast<uint32_t>(payload.size());
    AppendU8(&tag, 9);  // Video tag.
    AppendU24(&tag, body_size);
    AppendFlvTimestamp(&tag, timestamp_ms);
    AppendU24(&tag, 0);  // FLV StreamID is always 0.
    AppendU8(&tag,
               static_cast<uint8_t>(((keyframe ? 1 : 2) << 4) | kFlvCodecIdAvc));
    AppendU8(&tag, avc_packet_type);
    AppendU24(&tag, static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU);
    tag.append(payload);
    AppendU32(&tag, body_size + 11U);  // PreviousTagSize.
    return tag;
}

std::string BuildEnhancedFlvVideoTag(bool keyframe, uint8_t packet_type,
                                     int32_t composition_time_ms,
                                     uint32_t timestamp_ms,
                                     const std::string &payload) {
    // Enhanced FLV for H.265 replaces FLV's legacy CodecID with:
    // ExHeader + frame type + packet type, then FourCC "hvc1".
    // 这里不是普通 FLV 的 CodecID=7，而是 Enhanced FLV 扩展头 + hvc1 FourCC，
    // 否则浏览器侧 mpegts.js 无法按 H.265 解释后面的长度前缀 NAL。
    std::string tag;
    const uint32_t body_size = 8U + static_cast<uint32_t>(payload.size());
    AppendU8(&tag, 9);  // Video tag.
    AppendU24(&tag, body_size);
    AppendFlvTimestamp(&tag, timestamp_ms);
    AppendU24(&tag, 0);  // FLV StreamID is always 0.
    AppendU8(&tag,
               static_cast<uint8_t>(kEnhancedFlvHeader |
                                    (keyframe ? kFlvFrameKey : kFlvFrameInter) |
                                    packet_type));
    AppendFourCc(&tag, "hvc1");
    AppendU24(&tag, static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU);
    tag.append(payload);
    AppendU32(&tag, body_size + 11U);  // PreviousTagSize.
    return tag;
}

}  // namespace

RtpPacketizer::RtpPacketizer(uint32_t mtu_bytes)
    : mtu_bytes_(mtu_bytes < 64 ? 64 : mtu_bytes) {}

bool RtpPacketizer::Packetize(
    const EncodedFrame &frame,
    uint16_t *sequence,
    uint32_t ssrc,
    IRtpPacketSink *sink) const {
    if (!frame.HasValidPayload() || sequence == nullptr || sink == nullptr) {
        return false;
    }
    const uint8_t *payload = frame.PayloadData();
    const size_t size = frame.size;

    if (frame.codec == VideoCodec::kH265) {
        stream_codec::H265NalUnitList units;
        if (!stream_codec::ParseH265AnnexBNalUnits(payload, size, &units)) {
            return false;
        }
        for (size_t i = 0; i < units.count; ++i) {
            const stream_codec::H265NalUnit &unit = units.units[i];
            if (unit.size >
                static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            if (!PacketizeNal(frame, unit.data, static_cast<uint32_t>(unit.size),
                              i + 1 == units.count, sequence, ssrc, sink)) {
                return false;
            }
        }
        if (!units.empty()) {
            return true;
        }
    } else {
        stream_codec::H264NalUnitList units;
        if (!stream_codec::ParseH264AnnexBNalUnits(payload, size, &units)) {
            return false;
        }
        for (size_t i = 0; i < units.count; ++i) {
            const stream_codec::H264NalUnit &unit = units.units[i];
            if (unit.size >
                static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            if (!PacketizeNal(frame, unit.data, static_cast<uint32_t>(unit.size),
                              i + 1 == units.count, sequence, ssrc, sink)) {
                return false;
            }
        }
        if (!units.empty()) {
            return true;
        }
    }
    return PacketizeNal(frame, payload, static_cast<uint32_t>(size), true,
                        sequence, ssrc, sink);
}

bool RtpPacketizer::SendRtpPacket(
    const EncodedFrame &frame, const uint8_t *prefix, uint32_t prefix_size,
    const uint8_t *payload, uint32_t size, bool marker, uint16_t *sequence,
    uint32_t ssrc, IRtpPacketSink *sink) const {
    if (payload == nullptr || size == 0 || sequence == nullptr ||
        sink == nullptr) {
        return false;
    }
    uint8_t header[kRtpHeaderSize];
    WriteRtpHeader(frame, marker, *sequence, ssrc, header);
    ++(*sequence);
    RtpPacketView packet;
    packet.marker = marker;
    if (!packet.AddHeader(header, sizeof(header)) ||
        !packet.AddHeader(prefix, prefix_size) ||
        !packet.AddPayload(payload, size)) {
        return false;
    }
    return sink->OnRtpPacket(packet);
}

bool RtpPacketizer::PacketizeNal(const EncodedFrame &frame,
                                 const uint8_t *payload, uint32_t size,
                                 bool marker, uint16_t *sequence,
                                 uint32_t ssrc, IRtpPacketSink *sink) const {
    if (payload == nullptr || size == 0) {
        return false;
    }
    if (size + kRtpHeaderSize <= mtu_bytes_) {
        return SendRtpPacket(frame, nullptr, 0, payload, size, marker, sequence,
                             ssrc, sink);
    }
    if (frame.codec == VideoCodec::kH265) {
        return PacketizeH265(frame, payload, size, marker, sequence, ssrc, sink);
    }
    return PacketizeH264(frame, payload, size, marker, sequence, ssrc, sink);
}

bool RtpPacketizer::PacketizeH264(const EncodedFrame &frame,
                                  const uint8_t *payload, uint32_t size,
                                  bool marker, uint16_t *sequence,
                                  uint32_t ssrc, IRtpPacketSink *sink) const {
    if (size <= 1) {
        return false;
    }
    const uint8_t nal = payload[0];
    const uint8_t fu_indicator = (nal & 0xe0) | kH264FuANalType;
    const uint8_t nal_type = nal & 0x1f;
    const uint32_t max_fragment = mtu_bytes_ - kH264FuOverhead;
    uint32_t offset = 1;
    bool start = true;
    while (offset < size) {
        const uint32_t chunk = std::min(max_fragment, size - offset);
        uint8_t fu[2] = {fu_indicator, nal_type};
        uint8_t fu_header = nal_type;
        if (start) {
            fu_header |= 0x80;
        }
        if (offset + chunk >= size) {
            fu_header |= 0x40;
        }
        fu[1] = fu_header;
        if (!SendRtpPacket(frame, fu, sizeof(fu), payload + offset, chunk,
                           marker && offset + chunk >= size, sequence, ssrc,
                           sink)) {
            return false;
        }
        offset += chunk;
        start = false;
    }
    return true;
}

bool RtpPacketizer::PacketizeH265(const EncodedFrame &frame,
                                  const uint8_t *payload, uint32_t size,
                                  bool marker, uint16_t *sequence,
                                  uint32_t ssrc, IRtpPacketSink *sink) const {
    if (size <= 2) {
        return false;
    }
    const uint8_t nal0 = payload[0];
    const uint8_t nal1 = payload[1];
    const uint8_t nal_type = (nal0 >> 1) & 0x3f;
    const uint32_t max_fragment = mtu_bytes_ - kH265FuOverhead;
    uint32_t offset = 2;
    bool start = true;
    while (offset < size) {
        const uint32_t chunk = std::min(max_fragment, size - offset);
        uint8_t fu[3] = {
            static_cast<uint8_t>((nal0 & 0x81) | (kH265FuNalType << 1)), nal1,
            nal_type};
        uint8_t fu_header = nal_type;
        if (start) {
            fu_header |= 0x80;
        }
        if (offset + chunk >= size) {
            fu_header |= 0x40;
        }
        fu[2] = fu_header;
        if (!SendRtpPacket(frame, fu, sizeof(fu), payload + offset, chunk,
                           marker && offset + chunk >= size, sequence, ssrc,
                           sink)) {
            return false;
        }
        offset += chunk;
        start = false;
    }
    return true;
}

std::string BuildFlvFileHeader() {
    // FLV file header for a live HTTP-FLV response: signature, version, video
    // flag, header size, then PreviousTagSize0.
    std::string header;
    header.append("FLV", 3);
    AppendU8(&header, 1);  // FLV version.
    AppendU8(&header, 1);  // Video-only flags.
    AppendU32(&header, 9);   // FLV header size.
    AppendU32(&header, 0);   // PreviousTagSize0.
    return header;
}

std::string BuildH264FlvSequenceHeaderTag(const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
    return BuildH264FlvVideoTag(true, kFlvPacketTypeSequenceStart, 0,
                                timestamp_ms,
                                BuildH264FlvConfigurationRecord(sps, pps));
}

std::string BuildH265FlvSequenceHeaderTag(const std::string &vps,
                                          const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
    const std::string payload = BuildH265HvccRecord(vps, sps, pps);
    std::string tag;
    const uint32_t body_size = 5U + static_cast<uint32_t>(payload.size());
    AppendU8(&tag, 9);  // Video tag.
    AppendU24(&tag, body_size);
    AppendFlvTimestamp(&tag, timestamp_ms);
    AppendU24(&tag, 0);  // FLV StreamID is always 0.
    AppendU8(&tag, static_cast<uint8_t>(kEnhancedFlvHeader | kFlvFrameKey |
                                          kFlvPacketTypeSequenceStart));
    AppendFourCc(&tag, "hvc1");
    tag.append(payload);
    AppendU32(&tag, body_size + 11U);  // PreviousTagSize.
    return tag;
}

std::string BuildH264FlvVideoTag(bool keyframe, int32_t composition_time_ms,
                                 uint32_t timestamp_ms,
                                 const std::string &avcc_sample) {
    return BuildH264FlvVideoTag(keyframe, kFlvPacketTypeCodedFrames,
                                composition_time_ms, timestamp_ms, avcc_sample);
}

std::string BuildH265FlvVideoTag(bool keyframe, int32_t composition_time_ms,
                                 uint32_t timestamp_ms,
                                 const std::string &length_prefixed_sample) {
    return BuildEnhancedFlvVideoTag(keyframe, kFlvPacketTypeCodedFrames,
                                    composition_time_ms, timestamp_ms,
                                    length_prefixed_sample);
}

std::string BuildTsSegmentHeader(VideoCodec codec, TsMuxerState *state) {
    if (state == nullptr) {
        return std::string();
    }
    // Each HLS .ts segment starts with PAT and PMT. PMT is codec-dependent so a
    // standalone segment tells the browser whether the video PID is AVC or HEVC.
    std::string header = BuildPatPacket(&state->pat_continuity);
    header += BuildPmtPacket(codec, &state->pmt_continuity);
    return header;
}

void AppendVideoAccessUnitToTsSegment(VideoCodec codec,
                                      const std::string &access_unit,
                                      int64_t pts_us, int64_t dts_us,
                                      TsMuxerState *state,
                                      std::string *segment_body) {
    if ((codec != VideoCodec::kH264 && codec != VideoCodec::kH265) ||
        access_unit.empty() || state == nullptr || segment_body == nullptr) {
        return;
    }
    const uint64_t pts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, pts_us) * 9 / 100);
    const uint64_t dts_90k =
        static_cast<uint64_t>(std::max<int64_t>(0, dts_us) * 9 / 100);
    // PTS/DTS from media frames are microseconds; TS/PES timestamps use 90 kHz.
    AppendTsPayload(BuildPesPacket(access_unit, pts_90k, dts_90k), dts_90k,
                    &state->video_continuity, segment_body);
}

}  // namespace stream_mux
}  // namespace live_stream
