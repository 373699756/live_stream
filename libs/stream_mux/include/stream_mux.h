#ifndef LIVE_STREAM_STREAM_MUX_H_
#define LIVE_STREAM_STREAM_MUX_H_

#include "media/encoded_frame.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {
namespace stream_codec {
struct H264NalUnitList;
}  // namespace stream_codec

namespace stream_mux {

constexpr size_t kMaxRtpPacketSlices = 4;

struct RtpPacketSlice {
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool media_payload = false;
};

struct RtpPacketView {
    RtpPacketSlice slices[kMaxRtpPacketSlices];
    size_t slice_count = 0;
    bool marker = false;

    bool AddHeader(const uint8_t *data, size_t size) {
        return Add(data, size, false);
    }

    bool AddPayload(const uint8_t *data, size_t size) {
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

class RtpPacketizer {
public:
    explicit RtpPacketizer(uint32_t mtu_bytes);

    bool Packetize(const EncodedFrame &frame, uint16_t *sequence, uint32_t ssrc,
                   IRtpPacketSink *sink) const;

private:
    bool SendRtpPacket(const EncodedFrame &frame, const uint8_t *prefix,
                       uint32_t prefix_size, const uint8_t *payload,
                       uint32_t size, bool marker, uint16_t *sequence,
                       uint32_t ssrc, IRtpPacketSink *sink) const;
    bool PacketizeNal(const EncodedFrame &frame, const uint8_t *payload,
                      uint32_t size, bool marker, uint16_t *sequence,
                      uint32_t ssrc, IRtpPacketSink *sink) const;
    bool PacketizeH264(const EncodedFrame &frame, const uint8_t *payload,
                       uint32_t size, bool marker, uint16_t *sequence,
                       uint32_t ssrc, IRtpPacketSink *sink) const;
    bool PacketizeH265(const EncodedFrame &frame, const uint8_t *payload,
                       uint32_t size, bool marker, uint16_t *sequence,
                       uint32_t ssrc, IRtpPacketSink *sink) const;

    uint32_t mtu_bytes_ = 1200;
};

struct TsMuxerState {
    uint8_t pat_continuity = 0;
    uint8_t pmt_continuity = 0;
    uint8_t video_continuity = 0;
};

std::string BuildFlvFileHeader();

std::string BuildH264FlvSequenceHeaderTag(const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms);

std::string BuildH264FlvVideoTag(bool keyframe, int32_t composition_time_ms,
                                 uint32_t timestamp_ms,
                                 const stream_codec::H264NalUnitList &units);

std::string BuildTsSegmentHeader(VideoCodec codec, TsMuxerState *state);

void AppendVideoAccessUnitToTsSegment(VideoCodec codec,
                                      const std::string &access_unit,
                                      int64_t pts_us, int64_t dts_us,
                                      TsMuxerState *state,
                                      std::string *segment_body);

}  // namespace stream_mux
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_MUX_H_
