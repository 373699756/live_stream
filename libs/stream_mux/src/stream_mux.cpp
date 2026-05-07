#include "stream_mux.h"

#include "stream_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace stream_mux {
namespace {

constexpr uint8_t kPayloadTypeH264 = 96;
constexpr uint8_t kPayloadTypeH265 = 98;
constexpr uint32_t kRtpClockRate = 90000;
constexpr uint16_t kPatPid = 0x0000;
constexpr uint16_t kPmtPid = 0x1000;
constexpr uint16_t kVideoPid = 0x0100;
constexpr uint8_t kTsPacketSize = 188;

uint8_t PayloadType(VideoCodec codec) {
  return codec == VideoCodec::kH265 ? kPayloadTypeH265 : kPayloadTypeH264;
}

uint32_t RtpTimestamp(const EncodedFrame &frame) {
  return static_cast<uint32_t>((frame.pts_us * kRtpClockRate) / 1000000);
}

size_t EstimatePacketCount(uint32_t size, uint32_t mtu_bytes,
                           VideoCodec codec) {
  if (size + 12 <= mtu_bytes) {
    return 1;
  }
  const uint32_t max_fragment =
      codec == VideoCodec::kH265 ? mtu_bytes - 15 : mtu_bytes - 14;
  if (max_fragment == 0) {
    return 1;
  }
  return static_cast<size_t>((size + max_fragment - 1) / max_fragment);
}

void AppendU16(std::string *out, uint16_t value) {
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

void AppendU24(std::string *out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

void AppendU32(std::string *out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 24) & 0xff));
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

void AppendU16(std::vector<uint8_t> *out, uint16_t value) {
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  out->push_back(static_cast<uint8_t>(value & 0xff));
}

void AppendU32(std::vector<uint8_t> *out, uint32_t value) {
  out->push_back(static_cast<uint8_t>((value >> 24) & 0xff));
  out->push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  out->push_back(static_cast<uint8_t>(value & 0xff));
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

std::string BuildPatPacket(uint8_t *continuity_counter) {
  std::string section;
  section.push_back('\x00');
  section.push_back('\xb0');
  section.push_back('\x0d');
  AppendU16(&section, 1);
  section.push_back('\xc1');
  section.push_back('\x00');
  section.push_back('\x00');
  AppendU16(&section, 1);
  section.push_back(static_cast<char>(0xe0 | ((kPmtPid >> 8) & 0x1f)));
  section.push_back(static_cast<char>(kPmtPid & 0xff));
  const uint32_t crc = MpegCrc32(
      reinterpret_cast<const uint8_t *>(section.data()), section.size());
  AppendU32(&section, crc);

  std::string packet(kTsPacketSize, static_cast<char>(0xff));
  packet[0] = '\x47';
  packet[1] = '\x40';
  packet[2] = '\x00';
  packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
  *continuity_counter = static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
  packet[4] = '\x00';
  std::copy(section.begin(), section.end(), packet.begin() + 5);
  return packet;
}

std::string BuildPmtPacket(uint8_t *continuity_counter) {
  std::string section;
  section.push_back('\x02');
  section.push_back('\xb0');
  section.push_back('\x12');
  AppendU16(&section, 1);
  section.push_back('\xc1');
  section.push_back('\x00');
  section.push_back('\x00');
  section.push_back(static_cast<char>(0xe0 | ((kVideoPid >> 8) & 0x1f)));
  section.push_back(static_cast<char>(kVideoPid & 0xff));
  section.push_back('\xf0');
  section.push_back('\x00');
  section.push_back('\x1b');
  section.push_back(static_cast<char>(0xe0 | ((kVideoPid >> 8) & 0x1f)));
  section.push_back(static_cast<char>(kVideoPid & 0xff));
  section.push_back('\xf0');
  section.push_back('\x00');
  const uint32_t crc = MpegCrc32(
      reinterpret_cast<const uint8_t *>(section.data()), section.size());
  AppendU32(&section, crc);

  std::string packet(kTsPacketSize, static_cast<char>(0xff));
  packet[0] = '\x47';
  packet[1] = static_cast<char>(0x40 | ((kPmtPid >> 8) & 0x1f));
  packet[2] = static_cast<char>(kPmtPid & 0xff);
  packet[3] = static_cast<char>(0x10 | (*continuity_counter & 0x0f));
  *continuity_counter = static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);
  packet[4] = '\x00';
  std::copy(section.begin(), section.end(), packet.begin() + 5);
  return packet;
}

void AppendPts(std::string *out, uint8_t prefix, uint64_t value) {
  const uint64_t pts = value & 0x1ffffffffULL;
  out->push_back(
      static_cast<char>((prefix << 4) | (((pts >> 30) & 0x07) << 1) | 0x01));
  out->push_back(static_cast<char>((pts >> 22) & 0xff));
  out->push_back(static_cast<char>((((pts >> 15) & 0x7f) << 1) | 0x01));
  out->push_back(static_cast<char>((pts >> 7) & 0xff));
  out->push_back(static_cast<char>(((pts & 0x7f) << 1) | 0x01));
}

std::string BuildPesPacket(const std::string &access_unit, uint64_t pts_90k,
                           uint64_t dts_90k) {
  std::string pes;
  pes.append("\x00\x00\x01\xe0", 4);
  pes.append("\x00\x00", 2);
  pes.push_back('\x80');
  const bool has_dts = pts_90k != dts_90k;
  pes.push_back(has_dts ? '\xc0' : '\x80');
  pes.push_back(has_dts ? '\x0a' : '\x05');
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
  target[5] = '\x00';
}

void AppendTsPayload(const std::string &pes, uint64_t pcr_90k,
                     uint8_t *continuity_counter, std::string *out) {
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
    packet[0] = '\x47';
    packet[1] = static_cast<char>((first_packet ? 0x40 : 0x00) |
                                  ((kVideoPid >> 8) & 0x1f));
    packet[2] = static_cast<char>(kVideoPid & 0xff);
    packet[3] = static_cast<char>(((use_adaptation ? 0x30 : 0x10)) |
                                  (*continuity_counter & 0x0f));
    *continuity_counter =
        static_cast<uint8_t>((*continuity_counter + 1) & 0x0f);

    size_t packet_offset = 4;
    if (use_adaptation) {
      const size_t adaptation_length = adaptation_total - 1;
      packet[packet_offset++] = static_cast<char>(adaptation_length);
      packet[packet_offset++] = first_packet ? '\x10' : '\x00';
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
  std::string config;
  config.push_back('\x01');
  config.push_back(sps.size() > 1 ? sps[1] : '\x64');
  config.push_back(sps.size() > 2 ? sps[2] : '\x00');
  config.push_back(sps.size() > 3 ? sps[3] : '\x1f');
  config.push_back(static_cast<char>(0xff));
  config.push_back(static_cast<char>(0xe1));
  AppendU16(&config, static_cast<uint16_t>(sps.size()));
  config.append(sps);
  config.push_back('\x01');
  AppendU16(&config, static_cast<uint16_t>(pps.size()));
  config.append(pps);
  return config;
}

std::string BuildH264FlvVideoTag(bool keyframe, uint8_t avc_packet_type,
                                 int32_t composition_time_ms,
                                 uint32_t timestamp_ms,
                                 const std::string &payload) {
  std::string tag;
  const uint32_t body_size = 5U + static_cast<uint32_t>(payload.size());
  tag.push_back('\x09');
  AppendU24(&tag, body_size);
  AppendU24(&tag, timestamp_ms & 0x00ffffffU);
  tag.push_back(static_cast<char>((timestamp_ms >> 24) & 0xff));
  tag.append("\x00\x00\x00", 3);
  tag.push_back(static_cast<char>(((keyframe ? 1 : 2) << 4) | 7));
  tag.push_back(static_cast<char>(avc_packet_type));
  AppendU24(&tag, static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU);
  tag.append(payload);
  AppendU32(&tag, body_size + 11U);
  return tag;
}

}  // namespace

RtpPacketizer::RtpPacketizer(uint32_t mtu_bytes)
    : mtu_bytes_(mtu_bytes < 64 ? 64 : mtu_bytes) {}

std::vector<RtpPacket> RtpPacketizer::Packetize(const EncodedFrame &frame,
                                                uint16_t *sequence,
                                                uint32_t ssrc) const {
  std::vector<RtpPacket> packets;
  if (frame.buffer == nullptr || sequence == nullptr ||
      frame.offset > frame.buffer->Size() ||
      frame.size > frame.buffer->Size() - frame.offset) {
    return packets;
  }
  const uint8_t *payload = frame.buffer->Data() + frame.offset;
  size_t size = frame.size;
  stream_codec::StripAnnexBStartCode(&payload, &size);
  if (size == 0) {
    return packets;
  }
  packets.reserve(EstimatePacketCount(static_cast<uint32_t>(size), mtu_bytes_,
                                      frame.codec));
  if (size + 12 <= mtu_bytes_) {
    SendRtpPacket(frame, payload, static_cast<uint32_t>(size), true, sequence,
                  ssrc, &packets);
    return packets;
  }
  if (frame.codec == VideoCodec::kH265) {
    PacketizeH265(frame, payload, static_cast<uint32_t>(size), sequence, ssrc,
                  &packets);
  } else {
    PacketizeH264(frame, payload, static_cast<uint32_t>(size), sequence, ssrc,
                  &packets);
  }
  return packets;
}

void RtpPacketizer::SendRtpPacket(const EncodedFrame &frame,
                                  const uint8_t *payload, uint32_t size,
                                  bool marker, uint16_t *sequence,
                                  uint32_t ssrc,
                                  std::vector<RtpPacket> *packets) const {
  RtpPacket packet;
  packet.marker = marker;
  packet.bytes.reserve(size + 16);
  packet.bytes.push_back(0x80);
  packet.bytes.push_back(static_cast<uint8_t>((marker ? 0x80 : 0x00) |
                                              PayloadType(frame.codec)));
  AppendU16(&packet.bytes, (*sequence)++);
  AppendU32(&packet.bytes, RtpTimestamp(frame));
  AppendU32(&packet.bytes, ssrc);
  packet.bytes.insert(packet.bytes.end(), payload, payload + size);
  packets->push_back(std::move(packet));
}

void RtpPacketizer::PacketizeH264(const EncodedFrame &frame,
                                  const uint8_t *payload, uint32_t size,
                                  uint16_t *sequence, uint32_t ssrc,
                                  std::vector<RtpPacket> *packets) const {
  if (size <= 1) {
    return;
  }
  const uint8_t nal = payload[0];
  const uint8_t fu_indicator = (nal & 0xe0) | 28;
  const uint8_t nal_type = nal & 0x1f;
  const uint32_t max_fragment = mtu_bytes_ - 14;
  uint32_t offset = 1;
  bool start = true;
  while (offset < size) {
    const uint32_t chunk = std::min(max_fragment, size - offset);
    std::vector<uint8_t> fragment;
    fragment.reserve(chunk + 2);
    fragment.push_back(fu_indicator);
    uint8_t fu_header = nal_type;
    if (start) {
      fu_header |= 0x80;
    }
    if (offset + chunk >= size) {
      fu_header |= 0x40;
    }
    fragment.push_back(fu_header);
    fragment.insert(fragment.end(), payload + offset, payload + offset + chunk);
    SendRtpPacket(frame, fragment.data(),
                  static_cast<uint32_t>(fragment.size()),
                  offset + chunk >= size, sequence, ssrc, packets);
    offset += chunk;
    start = false;
  }
}

void RtpPacketizer::PacketizeH265(const EncodedFrame &frame,
                                  const uint8_t *payload, uint32_t size,
                                  uint16_t *sequence, uint32_t ssrc,
                                  std::vector<RtpPacket> *packets) const {
  if (size <= 2) {
    return;
  }
  const uint8_t nal0 = payload[0];
  const uint8_t nal1 = payload[1];
  const uint8_t nal_type = (nal0 >> 1) & 0x3f;
  const uint32_t max_fragment = mtu_bytes_ - 15;
  uint32_t offset = 2;
  bool start = true;
  while (offset < size) {
    const uint32_t chunk = std::min(max_fragment, size - offset);
    std::vector<uint8_t> fragment;
    fragment.reserve(chunk + 3);
    fragment.push_back((nal0 & 0x81) | (49 << 1));
    fragment.push_back(nal1);
    uint8_t fu_header = nal_type;
    if (start) {
      fu_header |= 0x80;
    }
    if (offset + chunk >= size) {
      fu_header |= 0x40;
    }
    fragment.push_back(fu_header);
    fragment.insert(fragment.end(), payload + offset, payload + offset + chunk);
    SendRtpPacket(frame, fragment.data(),
                  static_cast<uint32_t>(fragment.size()),
                  offset + chunk >= size, sequence, ssrc, packets);
    offset += chunk;
    start = false;
  }
}

std::string BuildFlvFileHeader() {
  std::string header;
  header.append("FLV", 3);
  header.push_back('\x01');
  header.push_back('\x01');
  header.append("\x00\x00\x00\x09", 4);
  header.append("\x00\x00\x00\x00", 4);
  return header;
}

std::string BuildH264FlvSequenceHeaderTag(const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
  return BuildH264FlvVideoTag(
      true, 0, 0, timestamp_ms, BuildH264FlvConfigurationRecord(sps, pps));
}

std::string BuildH264FlvVideoTag(bool keyframe, int32_t composition_time_ms,
                                 uint32_t timestamp_ms,
                                 const std::string &avcc_sample) {
  return BuildH264FlvVideoTag(keyframe, 1, composition_time_ms, timestamp_ms,
                              avcc_sample);
}

std::string BuildTsSegmentHeader(TsMuxerState *state) {
  if (state == nullptr) {
    return std::string();
  }
  std::string header = BuildPatPacket(&state->pat_continuity);
  header += BuildPmtPacket(&state->pmt_continuity);
  return header;
}

void AppendH264AccessUnitToTsSegment(const std::string &access_unit,
                                     int64_t pts_us, int64_t dts_us,
                                     TsMuxerState *state,
                                     std::string *segment_body) {
  if (state == nullptr || segment_body == nullptr) {
    return;
  }
  const uint64_t pts_90k =
      static_cast<uint64_t>(std::max<int64_t>(0, pts_us) * 9 / 100);
  const uint64_t dts_90k =
      static_cast<uint64_t>(std::max<int64_t>(0, dts_us) * 9 / 100);
  AppendTsPayload(BuildPesPacket(access_unit, pts_90k, dts_90k), dts_90k,
                  &state->video_continuity, segment_body);
}

}  // namespace stream_mux
}  // namespace live_stream
