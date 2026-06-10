#ifndef LIVE_STREAM_RTP_RTP_H_
#define LIVE_STREAM_RTP_RTP_H_

#include "media/encoded_frame.h"

#include <cstddef>
#include <cstdint>

namespace live_stream {
namespace rtp {

constexpr size_t kMaxRtpPacketSlices = 4;
constexpr uint8_t kRtpPayloadTypeH264 = 96;
constexpr uint8_t kRtpPayloadTypeH265 = 98;
constexpr uint32_t kRtpClockRate = 90000;
constexpr size_t kRtpHeaderSize = 12;
constexpr size_t kMaxRtpPayloadHeaderSize = 3;

uint32_t RtpTimestampFromPtsUs(int64_t pts_us,
                               uint32_t clock_rate = kRtpClockRate);
bool IsRtpTimestampBackwards(uint32_t timestamp,
                             uint32_t previous_timestamp);

struct RtpPacketSlice {
    // RTP packet 用多个 slice 描述，避免把 header、FU header 和媒体 payload
    // 拼成一块临时大 buffer。
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool media_payload = false;
};

struct RtpPacketView {
    // packet view 只在 OnRtpPacket 回调期间有效。media_payload=true 的 slice
    // 指向输入 EncodedFrame payload，异步发送方必须自己持有底层 VideoBuffer。
    RtpPacketSlice slices[kMaxRtpPacketSlices];
    size_t slice_count = 0;
    bool marker = false;
    uint8_t payload_type = 0;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    uint8_t rtp_header[kRtpHeaderSize] = {};
    uint8_t payload_header[kMaxRtpPayloadHeaderSize] = {};

    bool SetRtpHeader(const uint8_t *data, size_t size) {
        if (data == nullptr || size != sizeof(rtp_header)) {
            return false;
        }
        for (size_t i = 0; i < size; ++i) {
            rtp_header[i] = data[i];
        }
        return Add(rtp_header, size, false);
    }

    bool SetPayloadHeader(const uint8_t *data, size_t size) {
        if (size == 0) {
            return true;
        }
        if (data == nullptr || size > sizeof(payload_header)) {
            return false;
        }
        for (size_t i = 0; i < size; ++i) {
            payload_header[i] = data[i];
        }
        return Add(payload_header, size, false);
    }

    bool SetPayload(const uint8_t *data, size_t size) {
        return Add(data, size, true);
    }

    size_t Size() const {
        size_t total = 0;
        for (size_t i = 0; i < slice_count; ++i) {
            total += slices[i].size;
        }
        return total;
    }

private:
    bool Add(const uint8_t *data, size_t size, bool media_payload) {
        if (size == 0) {
            return true;
        }
        if (data == nullptr || slice_count >= kMaxRtpPacketSlices) {
            return false;
        }
        slices[slice_count].data = data;
        slices[slice_count].size = size;
        slices[slice_count].media_payload = media_payload;
        ++slice_count;
        return true;
    }
};

class IRtpPacketSink {
public:
    virtual ~IRtpPacketSink() = default;

    virtual bool OnRtpPacket(const RtpPacketView &packet) = 0;
};

struct RtpPacketizerOptions {
    uint32_t mtu_bytes = 1200;
    uint8_t h264_payload_type = kRtpPayloadTypeH264;
    uint8_t h265_payload_type = kRtpPayloadTypeH265;
};

uint8_t RtpPayloadTypeForCodec(VideoCodec codec);
uint8_t RtpPayloadTypeForCodec(const RtpPacketizerOptions &options,
                               VideoCodec codec);

struct RtpPacketizerInput {
    VideoCodec codec = VideoCodec::kH264;
    // payload 是一帧 AnnexB 视频码流；RTP 模块只分片，不拥有 SDP、SRTP、
    // socket/session，也不做 FLV/HLS 封装。
    const uint8_t *payload = nullptr;
    size_t payload_size = 0;
    // pts_us 必须使用 media_source 修正后的 PTS，packetizer 内部转换成
    // 90kHz RTP timestamp。
    int64_t pts_us = 0;
    // sequence 由调用方保存跨帧递增状态；每发送一个 RTP packet 本模块递增一次。
    uint16_t *sequence = nullptr;
    uint32_t ssrc = 0;
    uint8_t payload_type = 0;
};

class RtpPacketizer {
public:
    explicit RtpPacketizer(uint32_t mtu_bytes);
    explicit RtpPacketizer(const RtpPacketizerOptions &options);

    bool Packetize(const RtpPacketizerInput &input,
                   IRtpPacketSink *sink) const;
    bool Packetize(const EncodedFrame &frame, uint16_t *sequence, uint32_t ssrc,
                   IRtpPacketSink *sink) const;

private:
    bool SendRtpPacket(const RtpPacketizerInput &input, const uint8_t *prefix,
                       uint32_t prefix_size, const uint8_t *payload,
                       uint32_t size, bool marker,
                       IRtpPacketSink *sink) const;
    bool PacketizeNal(const RtpPacketizerInput &input, const uint8_t *payload,
                      uint32_t size, bool marker, IRtpPacketSink *sink) const;
    bool PacketizeH264(const RtpPacketizerInput &input, const uint8_t *payload,
                       uint32_t size, bool marker,
                       IRtpPacketSink *sink) const;
    bool PacketizeH265(const RtpPacketizerInput &input, const uint8_t *payload,
                       uint32_t size, bool marker,
                       IRtpPacketSink *sink) const;

    RtpPacketizerOptions options_;
};

}  // namespace rtp
}  // namespace live_stream

#endif  // LIVE_STREAM_RTP_RTP_H_
