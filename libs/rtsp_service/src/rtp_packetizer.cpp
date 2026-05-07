#include "rtp_packetizer.h"

#include <algorithm>

namespace live_stream {
namespace rtsp_internal {
namespace {

constexpr uint8_t kPayloadTypeH264 = 96;
constexpr uint8_t kPayloadTypeH265 = 98;
constexpr uint32_t kRtpClockRate = 90000;

uint8_t PayloadType(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? kPayloadTypeH265 : kPayloadTypeH264;
}

uint32_t RtpTimestamp(const EncodedFrame& frame) {
    return static_cast<uint32_t>((frame.pts_us * kRtpClockRate) / 1000000);
}

size_t EstimatePacketCount(uint32_t size,
                           uint32_t mtu_bytes,
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

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>(value & 0xff));
}

void StripAnnexBStartCode(const uint8_t** payload, uint32_t* size) {
    if (*size >= 4 && (*payload)[0] == 0 && (*payload)[1] == 0 &&
        (*payload)[2] == 0 && (*payload)[3] == 1) {
        *payload += 4;
        *size -= 4;
        return;
    }
    if (*size >= 3 && (*payload)[0] == 0 && (*payload)[1] == 0 &&
        (*payload)[2] == 1) {
        *payload += 3;
        *size -= 3;
    }
}

}  // namespace

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>(value & 0xff));
}

RtpPacketizer::RtpPacketizer(uint32_t mtu_bytes)
    : mtu_bytes_(mtu_bytes < 64 ? 64 : mtu_bytes) {}

std::vector<RtpPacket> RtpPacketizer::Packetize(
    const EncodedFrame& frame,
    uint16_t* sequence,
    uint32_t ssrc) const {
    std::vector<RtpPacket> packets;
    const uint8_t* payload = frame.buffer->Data() + frame.offset;
    uint32_t size = frame.size;
    StripAnnexBStartCode(&payload, &size);
    if (size == 0) {
        return packets;
    }
    packets.reserve(EstimatePacketCount(size, mtu_bytes_, frame.codec));
    if (size + 12 <= mtu_bytes_) {
        SendRtpPacket(frame, payload, size, true, sequence, ssrc, &packets);
        return packets;
    }
    if (frame.codec == VideoCodec::kH265) {
        PacketizeH265(frame, payload, size, sequence, ssrc, &packets);
    } else {
        PacketizeH264(frame, payload, size, sequence, ssrc, &packets);
    }
    return packets;
}

void RtpPacketizer::SendRtpPacket(const EncodedFrame& frame,
                                  const uint8_t* payload,
                                  uint32_t size,
                                  bool marker,
                                  uint16_t* sequence,
                                  uint32_t ssrc,
                                  std::vector<RtpPacket>* packets) const {
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

void RtpPacketizer::PacketizeH264(const EncodedFrame& frame,
                                  const uint8_t* payload,
                                  uint32_t size,
                                  uint16_t* sequence,
                                  uint32_t ssrc,
                                  std::vector<RtpPacket>* packets) const {
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
        SendRtpPacket(frame, fragment.data(), static_cast<uint32_t>(fragment.size()),
                      offset + chunk >= size, sequence, ssrc, packets);
        offset += chunk;
        start = false;
    }
}

void RtpPacketizer::PacketizeH265(const EncodedFrame& frame,
                                  const uint8_t* payload,
                                  uint32_t size,
                                  uint16_t* sequence,
                                  uint32_t ssrc,
                                  std::vector<RtpPacket>* packets) const {
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
        SendRtpPacket(frame, fragment.data(), static_cast<uint32_t>(fragment.size()),
                      offset + chunk >= size, sequence, ssrc, packets);
        offset += chunk;
        start = false;
    }
}

}  // namespace rtsp_internal
}  // namespace live_stream
