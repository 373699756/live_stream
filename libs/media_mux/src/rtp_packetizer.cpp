#include "media_mux.h"

#include "byte_writer.h"
#include "media_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace live_stream {
namespace media_mux {
namespace {

constexpr uint32_t kMinRtpMtuBytes = 64;
constexpr uint32_t kH264FuOverhead =
    static_cast<uint32_t>(kRtpHeaderSize + 2);
constexpr uint32_t kH265FuOverhead =
    static_cast<uint32_t>(kRtpHeaderSize + 3);
constexpr uint8_t kH264FuANalType = 28;
constexpr uint8_t kH265FuNalType = 49;

uint8_t PayloadTypeForCodec(const RtpPacketizerOptions &options,
                            VideoCodec codec) {
    return codec == VideoCodec::kH265 ? options.h265_payload_type
                                      : options.h264_payload_type;
}

uint32_t RtpTimestamp(const RtpPacketizerInput &input) {
    if (input.pts_us <= 0) {
        return 0;
    }
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(input.pts_us) * kRtpClockRate) / 1000000U);
}

void WriteRtpHeader(const RtpPacketizerInput &input,
                    bool marker,
                    uint16_t sequence,
                    uint32_t timestamp,
                    uint8_t *header) {
    header[0] = 0x80;
    header[1] =
        static_cast<uint8_t>((marker ? 0x80 : 0x00) | input.payload_type);
    byte_writer::WriteU16(header + 2, sequence);
    byte_writer::WriteU32(header + 4, timestamp);
    byte_writer::WriteU32(header + 8, input.ssrc);
}

bool BuildRtpInput(const EncodedFrame &frame,
                   uint16_t *sequence,
                   uint32_t ssrc,
                   const RtpPacketizerOptions &options,
                   RtpPacketizerInput *input) {
    if (input == nullptr || !EncodedFrameHasPayload(&frame) ||
        sequence == nullptr) {
        return false;
    }
    input->codec = frame.codec;
    input->payload = EncodedFramePayloadData(&frame);
    input->payload_size = frame.size;
    input->pts_us = frame.pts_us;
    input->sequence = sequence;
    input->ssrc = ssrc;
    input->payload_type = PayloadTypeForCodec(options, frame.codec);
    return input->payload != nullptr && input->payload_size > 0;
}

RtpPacketizerInput NormalizeRtpInput(const RtpPacketizerInput &input,
                                     const RtpPacketizerOptions &options) {
    RtpPacketizerInput normalized = input;
    if (normalized.payload_type == 0) {
        normalized.payload_type = PayloadTypeForCodec(options, input.codec);
    }
    return normalized;
}

}  // namespace

RtpPacketizer::RtpPacketizer(uint32_t mtu_bytes)
    : options_(RtpPacketizerOptions{}) {
    options_.mtu_bytes = mtu_bytes < kMinRtpMtuBytes ? kMinRtpMtuBytes
                                                     : mtu_bytes;
}

RtpPacketizer::RtpPacketizer(const RtpPacketizerOptions &options)
    : options_(options) {
    if (options_.mtu_bytes < kMinRtpMtuBytes) {
        options_.mtu_bytes = kMinRtpMtuBytes;
    }
}

bool RtpPacketizer::Packetize(const EncodedFrame &frame,
                              uint16_t *sequence,
                              uint32_t ssrc,
                              IRtpPacketSink *sink) const {
    RtpPacketizerInput input;
    if (!BuildRtpInput(frame, sequence, ssrc, options_, &input)) {
        return false;
    }
    return Packetize(input, sink);
}

bool RtpPacketizer::Packetize(const RtpPacketizerInput &input,
                              IRtpPacketSink *sink) const {
    const RtpPacketizerInput normalized_input =
        NormalizeRtpInput(input, options_);
    if (normalized_input.payload == nullptr ||
        normalized_input.payload_size == 0 ||
        normalized_input.sequence == nullptr || normalized_input.ssrc == 0 ||
        normalized_input.payload_type == 0 || sink == nullptr ||
        (normalized_input.codec != VideoCodec::kH264 &&
         normalized_input.codec != VideoCodec::kH265) ||
        normalized_input.payload_size > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    if (normalized_input.codec == VideoCodec::kH265) {
        media_codec::H265NalUnitList units;
        if (!media_codec::ParseH265AnnexBNalUnits(
                normalized_input.payload, normalized_input.payload_size,
                &units)) {
            return false;
        }
        for (size_t i = 0; i < units.count; ++i) {
            const media_codec::H265NalUnit &unit = units.units[i];
            if (unit.size >
                static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            if (!PacketizeNal(normalized_input, unit.data,
                              static_cast<uint32_t>(unit.size),
                              i + 1 == units.count, sink)) {
                return false;
            }
        }
        if (!units.empty()) {
            return true;
        }
    } else {
        media_codec::H264NalUnitList units;
        if (!media_codec::ParseH264AnnexBNalUnits(
                normalized_input.payload, normalized_input.payload_size,
                &units)) {
            return false;
        }
        for (size_t i = 0; i < units.count; ++i) {
            const media_codec::H264NalUnit &unit = units.units[i];
            if (unit.size >
                static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            if (!PacketizeNal(normalized_input, unit.data,
                              static_cast<uint32_t>(unit.size),
                              i + 1 == units.count, sink)) {
                return false;
            }
        }
        if (!units.empty()) {
            return true;
        }
    }

    return PacketizeNal(normalized_input, normalized_input.payload,
                        static_cast<uint32_t>(normalized_input.payload_size),
                        true, sink);
}

bool RtpPacketizer::SendRtpPacket(const RtpPacketizerInput &input,
                                  const uint8_t *prefix,
                                  uint32_t prefix_size,
                                  const uint8_t *payload,
                                  uint32_t size,
                                  bool marker,
                                  IRtpPacketSink *sink) const {
    if (payload == nullptr || size == 0 || input.sequence == nullptr ||
        sink == nullptr) {
        return false;
    }

    const uint16_t sequence = *input.sequence;
    ++(*input.sequence);
    const uint32_t timestamp = RtpTimestamp(input);
    uint8_t header[kRtpHeaderSize];
    WriteRtpHeader(input, marker, sequence, timestamp, header);

    RtpPacketView packet;
    packet.marker = marker;
    packet.payload_type = input.payload_type;
    packet.sequence = sequence;
    packet.timestamp = timestamp;
    packet.ssrc = input.ssrc;
    if (!packet.SetRtpHeader(header, sizeof(header)) ||
        !packet.SetPayloadHeader(prefix, prefix_size) ||
        !packet.SetPayload(payload, size)) {
        return false;
    }
    return sink->OnRtpPacket(packet);
}

bool RtpPacketizer::PacketizeNal(const RtpPacketizerInput &input,
                                 const uint8_t *payload,
                                 uint32_t size,
                                 bool marker,
                                 IRtpPacketSink *sink) const {
    if (payload == nullptr || size == 0) {
        return false;
    }
    if (size + kRtpHeaderSize <= options_.mtu_bytes) {
        return SendRtpPacket(input, nullptr, 0, payload, size, marker, sink);
    }
    if (input.codec == VideoCodec::kH265) {
        return PacketizeH265(input, payload, size, marker, sink);
    }
    return PacketizeH264(input, payload, size, marker, sink);
}

bool RtpPacketizer::PacketizeH264(const RtpPacketizerInput &input,
                                  const uint8_t *payload,
                                  uint32_t size,
                                  bool marker,
                                  IRtpPacketSink *sink) const {
    if (size <= 1) {
        return false;
    }
    const uint8_t nal = payload[0];
    const uint8_t fu_indicator = (nal & 0xe0) | kH264FuANalType;
    const uint8_t nal_type = nal & 0x1f;
    const uint32_t max_fragment = options_.mtu_bytes - kH264FuOverhead;
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
        if (!SendRtpPacket(input, fu, sizeof(fu), payload + offset, chunk,
                           marker && offset + chunk >= size, sink)) {
            return false;
        }
        offset += chunk;
        start = false;
    }
    return true;
}

bool RtpPacketizer::PacketizeH265(const RtpPacketizerInput &input,
                                  const uint8_t *payload,
                                  uint32_t size,
                                  bool marker,
                                  IRtpPacketSink *sink) const {
    if (size <= 2) {
        return false;
    }
    const uint8_t nal0 = payload[0];
    const uint8_t nal1 = payload[1];
    const uint8_t nal_type = (nal0 >> 1) & 0x3f;
    const uint32_t max_fragment = options_.mtu_bytes - kH265FuOverhead;
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
        if (!SendRtpPacket(input, fu, sizeof(fu), payload + offset, chunk,
                           marker && offset + chunk >= size, sink)) {
            return false;
        }
        offset += chunk;
        start = false;
    }
    return true;
}

}  // namespace media_mux
}  // namespace live_stream
