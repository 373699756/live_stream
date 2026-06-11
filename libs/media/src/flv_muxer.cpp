#include "flv_muxer.h"

#include "byte_writer.h"
#include "media_codec.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace media_internal {
namespace {

constexpr uint8_t kFlvCodecIdAvc = 7;
constexpr uint8_t kEnhancedFlvHeader = 0x80;
constexpr uint32_t kFlvMaxBodySize = 0x00ffffffU;
constexpr uint8_t kFlvPacketTypeSequenceStart = 0;
constexpr uint8_t kFlvPacketTypeCodedFrames = 1;
constexpr uint8_t kFlvFrameKey = 1 << 4;
constexpr uint8_t kFlvFrameInter = 2 << 4;

using byte_writer::AppendU24;
using byte_writer::AppendU32;
using byte_writer::AppendU8;

void WriteU24Bytes(uint32_t value, uint8_t *target) {
    target[0] = static_cast<uint8_t>((value >> 16) & 0xff);
    target[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    target[2] = static_cast<uint8_t>(value & 0xff);
}

void WriteU32Bytes(uint32_t value, uint8_t *target) {
    target[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    target[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    target[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    target[3] = static_cast<uint8_t>(value & 0xff);
}

void AppendFlvTimestamp(std::string *out, uint32_t timestamp_ms) {
    AppendU24(out, timestamp_ms & 0x00ffffffU);
    AppendU8(out, static_cast<uint8_t>((timestamp_ms >> 24) & 0xff));
}

void AppendFourCc(std::string *out, const char *fourcc) {
    if (out != nullptr && fourcc != nullptr) {
        out->append(fourcc, 4);
    }
}

bool IsH264FlvVideoNal(const media_codec::H264NalUnit &unit) {
    // FLV video tag 不重复写 SPS/PPS/AUD；参数集走 sequence header，
    // 真实图像 NAL 才写入 coded frames payload。
    return unit.data != nullptr && unit.size > 0 &&
           !media_codec::IsH264ParameterSetNal(unit.type) &&
           unit.type != media_codec::kH264NalTypeAud;
}

bool IsH265FlvVideoNal(const media_codec::H265NalUnit &unit) {
    return unit.data != nullptr && unit.size > 0 &&
           !media_codec::IsH265ParameterSetNal(unit.type) &&
           unit.type != media_codec::kH265NalTypeAud;
}

size_t H264FlvVideoPayloadSize(const media_codec::H264NalUnitList &units) {
    size_t payload_size = 0;
    for (const media_codec::H264NalUnit &unit : units) {
        if (IsH264FlvVideoNal(unit)) {
            payload_size += 4 + unit.size;
        }
    }
    return payload_size;
}

size_t H265FlvVideoPayloadSize(const media_codec::H265NalUnitList &units) {
    size_t payload_size = 0;
    for (const media_codec::H265NalUnit &unit : units) {
        if (IsH265FlvVideoNal(unit)) {
            payload_size += 4 + unit.size;
        }
    }
    return payload_size;
}

std::string BuildH264FlvSequenceHeaderTagBody(uint32_t timestamp_ms,
                                              const std::string &payload) {
    std::string tag;
    if (payload.size() > kFlvMaxBodySize - 5U) {
        return tag;
    }
    const uint32_t body_size = 5U + static_cast<uint32_t>(payload.size());
    // 标准 AVC FLV video tag body:
    // FrameType/CodecID + AVCPacketType + CompositionTime + avcC record。
    AppendU8(&tag, 9);
    AppendU24(&tag, body_size);
    AppendFlvTimestamp(&tag, timestamp_ms);
    AppendU24(&tag, 0);
    AppendU8(&tag, static_cast<uint8_t>((1 << 4) | kFlvCodecIdAvc));
    AppendU8(&tag, kFlvPacketTypeSequenceStart);
    AppendU24(&tag, 0);
    tag.append(payload);
    AppendU32(&tag, body_size + 11U);
    return tag;
}

std::string BuildEnhancedFlvVideoTag(bool keyframe,
                                     uint8_t packet_type,
                                     int32_t composition_time_ms,
                                     uint32_t timestamp_ms,
                                     const std::string &payload) {
    std::string tag;
    if (payload.size() > kFlvMaxBodySize - 8U) {
        return tag;
    }
    const uint32_t body_size = 8U + static_cast<uint32_t>(payload.size());
    tag.reserve(11U + body_size + 4U);
    // H.265 走 enhanced FLV：VideoTagHeader 带扩展标志，后面跟 FourCC "hvc1"
    // 和 composition time，再写 hvcC 或 length-prefixed NAL payload。
    AppendU8(&tag, 9);
    AppendU24(&tag, body_size);
    AppendFlvTimestamp(&tag, timestamp_ms);
    AppendU24(&tag, 0);
    AppendU8(&tag,
             static_cast<uint8_t>(kEnhancedFlvHeader |
                                  (keyframe ? kFlvFrameKey : kFlvFrameInter) |
                                  packet_type));
    AppendFourCc(&tag, "hvc1");
    AppendU24(&tag, static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU);
    tag.append(payload);
    AppendU32(&tag, body_size + 11U);
    return tag;
}

std::string BuildH264FlvSequenceHeaderTag(const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
    std::string record;
    if (!media_codec::BuildH264AvccRecord(sps, pps, &record)) {
        return std::string();
    }
    return BuildH264FlvSequenceHeaderTagBody(timestamp_ms, record);
}

std::string BuildH265FlvSequenceHeaderTag(const std::string &vps,
                                          const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
    std::string record;
    if (!media_codec::BuildH265HvccRecord(vps, sps, pps, &record)) {
        return std::string();
    }
    return BuildEnhancedFlvVideoTag(true, kFlvPacketTypeSequenceStart, 0,
                                    timestamp_ms, record);
}

bool BuildH264FlvVideoTagView(bool keyframe,
                              int32_t composition_time_ms,
                              uint32_t timestamp_ms,
                              const media_codec::H264NalUnitList &units,
                              FlvVideoTagView *tag) {
    if (tag == nullptr) {
        return false;
    }
    *tag = FlvVideoTagView{};
    const size_t payload_size = H264FlvVideoPayloadSize(units);
    if (payload_size == 0 || payload_size > kFlvMaxBodySize - 5U) {
        return false;
    }
    const uint32_t body_size = 5U + static_cast<uint32_t>(payload_size);
    uint8_t *header = tag->header;
    header[0] = 9;
    WriteU24Bytes(body_size, header + 1);
    WriteU24Bytes(timestamp_ms & 0x00ffffffU, header + 4);
    header[7] = static_cast<uint8_t>((timestamp_ms >> 24) & 0xff);
    WriteU24Bytes(0, header + 8);
    header[11] =
        static_cast<uint8_t>(((keyframe ? 1 : 2) << 4) | kFlvCodecIdAvc);
    header[12] = kFlvPacketTypeCodedFrames;
    // CompositionTime = PTS - DTS，FLV 使用 24 bit 有符号语义；这里按协议
    // 字节写入，输入时间戳已由 media 修正为相对单调时间。
    WriteU24Bytes(static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU,
                  header + 13);
    if (!tag->AddHeader(header, 16)) {
        return false;
    }
    size_t nal_index = 0;
    for (const media_codec::H264NalUnit &unit : units) {
        if (!IsH264FlvVideoNal(unit)) {
            continue;
        }
        if (nal_index >= media_codec::kMaxNalUnitsPerFrame ||
            !media_codec::WriteNalLengthPrefix(unit.size,
                                               tag->nal_lengths[nal_index])) {
            return false;
        }
        // FLV/AVCC payload 使用 4 字节长度前缀，而不是 AnnexB 起始码。
        // NAL 数据本身仍直接引用原始 EncodedFrame payload。
        if (!tag->AddHeader(tag->nal_lengths[nal_index], 4) ||
            !tag->AddPayload(unit.data, unit.size)) {
            return false;
        }
        ++nal_index;
    }
    WriteU32Bytes(body_size + 11U, tag->previous_tag_size);
    if (!tag->AddHeader(tag->previous_tag_size, 4)) {
        return false;
    }
    tag->timestamp_ms = timestamp_ms;
    return true;
}

bool BuildH265FlvVideoTagView(bool keyframe,
                              int32_t composition_time_ms,
                              uint32_t timestamp_ms,
                              const media_codec::H265NalUnitList &units,
                              FlvVideoTagView *tag) {
    if (tag == nullptr) {
        return false;
    }
    *tag = FlvVideoTagView{};
    const size_t payload_size = H265FlvVideoPayloadSize(units);
    if (payload_size == 0 || payload_size > kFlvMaxBodySize - 8U) {
        return false;
    }
    const uint32_t body_size = 8U + static_cast<uint32_t>(payload_size);
    uint8_t *header = tag->header;
    header[0] = 9;
    WriteU24Bytes(body_size, header + 1);
    WriteU24Bytes(timestamp_ms & 0x00ffffffU, header + 4);
    header[7] = static_cast<uint8_t>((timestamp_ms >> 24) & 0xff);
    WriteU24Bytes(0, header + 8);
    header[11] = static_cast<uint8_t>(kEnhancedFlvHeader |
                                      (keyframe ? kFlvFrameKey
                                                : kFlvFrameInter) |
                                      kFlvPacketTypeCodedFrames);
    header[12] = 'h';
    header[13] = 'v';
    header[14] = 'c';
    header[15] = '1';
    // enhanced FLV 的 H.265 coded frames 同样携带 composition time，
    // 便于有 B 帧时播放器按 PTS 呈现。
    WriteU24Bytes(static_cast<uint32_t>(composition_time_ms) & 0x00ffffffU,
                  header + 16);
    if (!tag->AddHeader(header, 19)) {
        return false;
    }
    size_t nal_index = 0;
    for (const media_codec::H265NalUnit &unit : units) {
        if (!IsH265FlvVideoNal(unit)) {
            continue;
        }
        if (nal_index >= media_codec::kMaxNalUnitsPerFrame ||
            !media_codec::WriteNalLengthPrefix(unit.size,
                                               tag->nal_lengths[nal_index])) {
            return false;
        }
        // 这里生成的是 slice view，不深拷贝视频 payload；HTTP-FLV 发送端
        // 必须在写完前保持 EncodedFrame 引用有效。
        if (!tag->AddHeader(tag->nal_lengths[nal_index], 4) ||
            !tag->AddPayload(unit.data, unit.size)) {
            return false;
        }
        ++nal_index;
    }
    WriteU32Bytes(body_size + 11U, tag->previous_tag_size);
    if (!tag->AddHeader(tag->previous_tag_size, 4)) {
        return false;
    }
    tag->timestamp_ms = timestamp_ms;
    return true;
}

}  // namespace

std::string FlvMuxer::BuildFileHeader() {
    std::string header;
    // 当前 FLV 只声明 video flag，不启用音频；产品范围也不包含音频链路。
    header.append("FLV", 3);
    AppendU8(&header, 1);
    AppendU8(&header, 1);
    AppendU32(&header, 9);
    AppendU32(&header, 0);
    return header;
}

std::string FlvMuxer::BuildSequenceHeader(Codec codec,
                                          const std::string &vps,
                                          const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
    if (codec == Codec::kH264) {
        if (sps.empty() || pps.empty()) {
            return std::string();
        }
        return BuildH264FlvSequenceHeaderTag(sps, pps, timestamp_ms);
    }
    if (codec == Codec::kH265) {
        if (vps.empty() || sps.empty() || pps.empty()) {
            return std::string();
        }
        return BuildH265FlvSequenceHeaderTag(vps, sps, pps, timestamp_ms);
    }
    return std::string();
}

bool FlvMuxer::BuildVideoTagView(const EncodedFrame &frame,
                                 const FramePayload &payload,
                                 bool keyframe,
                                 FlvVideoTagView *tag_view) {
    if (tag_view == nullptr || frame.codec != payload.encoded_frame.codec) {
        return false;
    }
    const int64_t composition_time_ms = (frame.pts_us - frame.dts_us) / 1000;
    const uint32_t timestamp_ms = static_cast<uint32_t>(frame.dts_us / 1000);
    // FLV 时间戳以 DTS 为基准，CompositionTime 单独表达 PTS 偏移。
    if (frame.codec == Codec::kH264) {
        return BuildH264FlvVideoTagView(
            keyframe, static_cast<int32_t>(composition_time_ms), timestamp_ms,
            payload.h264_units, tag_view);
    }
    if (frame.codec == Codec::kH265) {
        return BuildH265FlvVideoTagView(
            keyframe, static_cast<int32_t>(composition_time_ms), timestamp_ms,
            payload.h265_units, tag_view);
    }
    return false;
}

}  // namespace media_internal
}  // namespace live_stream
