#include "rtp.h"

#include "byte_writer.h"
#include "media_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace live_stream {
namespace rtp {

constexpr uint32_t kMinRtpMtuBytes = 64;
constexpr uint32_t kH264FuOverhead =
    static_cast<uint32_t>(kRtpHeaderSize + 2);
constexpr uint32_t kH265FuOverhead =
    static_cast<uint32_t>(kRtpHeaderSize + 3);
constexpr uint8_t kH264FuANalType = 28;
constexpr uint8_t kH265FuNalType = 49;

uint32_t RtpTimestampFromPtsUs(int64_t pts_us, uint32_t clock_rate) {
    if (pts_us <= 0 || clock_rate == 0) {
        return 0;
    }
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(pts_us) * clock_rate) / 1000000U);
}

bool IsRtpTimestampBackwards(uint32_t timestamp,
                             uint32_t previous_timestamp) {
    return static_cast<int32_t>(timestamp - previous_timestamp) < 0;
}

uint8_t RtpPayloadTypeForCodec(Codec codec) {
    return codec == Codec::kH265 ? kRtpPayloadTypeH265
                                 : kRtpPayloadTypeH264;
}

uint8_t RtpPayloadTypeForCodec(const RtpPacketizerOptions &options,
                               Codec codec) {
    return codec == Codec::kH265 ? options.h265_payload_type
                                 : options.h264_payload_type;
}

namespace {

uint32_t RtpTimestamp(const RtpPacketizerInput &input) {
    // 视频 RTP 固定使用 90kHz 时钟。输入是微秒时间戳，因此乘 90000 再除
    // 1000000 得到 RTP timestamp。
    return RtpTimestampFromPtsUs(input.pts_us);
}

void WriteRtpHeader(const RtpPacketizerInput &input,
                    bool marker,
                    uint16_t sequence,
                    uint32_t timestamp,
                    uint8_t *header) {
    // 这里只写最小 12 字节 RTP header：V=2，无扩展、无 CSRC。payload type、
    // sequence、timestamp、SSRC 均由调用方协商或维护。
    header[0] = 0x80;
    header[1] =
        static_cast<uint8_t>((marker ? 0x80 : 0x00) | input.payload_type);
    byte_writer::WriteU16(header + 2, sequence);
    byte_writer::WriteU32(header + 4, timestamp);
    byte_writer::WriteU32(header + 8, input.ssrc);
}

RtpPacketizerInput NormalizeRtpInput(const RtpPacketizerInput &input,
                                     const RtpPacketizerOptions &options) {
    RtpPacketizerInput normalized = input;
    if (normalized.payload_type == 0) {
        normalized.payload_type = RtpPayloadTypeForCodec(options,
                                                         input.codec);
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

bool RtpPacketizer::Packetize(const RtpPacketizerInput &input,
                              IRtpPacketSink *sink) const {
    const RtpPacketizerInput normalized_input =
        NormalizeRtpInput(input, options_);
    if (normalized_input.payload == nullptr ||
        normalized_input.payload_size == 0 ||
        normalized_input.sequence == nullptr || normalized_input.ssrc == 0 ||
        normalized_input.payload_type == 0 || sink == nullptr ||
        (normalized_input.codec != Codec::kH264 &&
         normalized_input.codec != Codec::kH265) ||
        normalized_input.payload_size > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    if (normalized_input.codec == Codec::kH265) {
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

    // 正常输入应为 AnnexB。保留兜底路径是为了上层已经传入单个裸 NAL 时仍能
    // 分片输出，但协议模块不应依赖它绕过 media_codec parser。
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
    // packet 的 slice 指针指向栈上 header 副本和输入 payload；sink 必须在
    // 回调内完成发送或复制自己需要的内容。
    // 这里不持有媒体 buffer owner；调用方必须在整个 Packetize 调用期间保证
    // input payload 生命周期。
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
        // 单个 NAL 可以放进 MTU 时直接作为一个 RTP payload 发送。
        return SendRtpPacket(input, nullptr, 0, payload, size, marker, sink);
    }
    if (input.codec == Codec::kH265) {
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
        // H.264 FU-A：第 1 字节保留原 NRI 并把 type 改为 28，第 2 字节携带
        // 原始 nal_unit_type 以及 start/end 标志。
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
        // H.265 FU：payload header 前 2 字节保留层级和 TID 信息，NAL type 改为
        // 49；第 3 字节携带原始 nal_unit_type 和 start/end 标志。
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

}  // namespace rtp
}  // namespace live_stream
