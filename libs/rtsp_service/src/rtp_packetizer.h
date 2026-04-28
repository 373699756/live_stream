#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTP_PACKETIZER_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTP_PACKETIZER_H_

#include "infra/encoded_frame.h"

#include <cstdint>
#include <vector>

namespace live_stream {
namespace rtsp_internal {

struct RtpPacket {
    std::vector<uint8_t> bytes;
    bool marker = false;
};

class RtpPacketizer {
 public:
    explicit RtpPacketizer(uint32_t mtu_bytes);

    std::vector<RtpPacket> Packetize(const infra::EncodedFrame& frame,
                                     uint16_t* sequence,
                                     uint32_t ssrc) const;

 private:
    void SendRtpPacket(const infra::EncodedFrame& frame,
                       const uint8_t* payload,
                       uint32_t size,
                       bool marker,
                       uint16_t* sequence,
                       uint32_t ssrc,
                       std::vector<RtpPacket>* packets) const;
    void PacketizeH264(const infra::EncodedFrame& frame,
                       const uint8_t* payload,
                       uint32_t size,
                       uint16_t* sequence,
                       uint32_t ssrc,
                       std::vector<RtpPacket>* packets) const;
    void PacketizeH265(const infra::EncodedFrame& frame,
                       const uint8_t* payload,
                       uint32_t size,
                       uint16_t* sequence,
                       uint32_t ssrc,
                       std::vector<RtpPacket>* packets) const;

    uint32_t mtu_bytes_ = 1200;
};

void AppendU16(std::vector<uint8_t>* out, uint16_t value);

}  // namespace rtsp_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_SRC_RTP_PACKETIZER_H_
