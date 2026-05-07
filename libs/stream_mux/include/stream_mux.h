#ifndef LIVE_STREAM_STREAM_MUX_H_
#define LIVE_STREAM_STREAM_MUX_H_

#include "media/encoded_frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace stream_mux {

struct RtpPacket {
  std::vector<uint8_t> bytes;
  bool marker = false;
};

class RtpPacketizer {
 public:
  explicit RtpPacketizer(uint32_t mtu_bytes);

  std::vector<RtpPacket> Packetize(const EncodedFrame &frame,
                                   uint16_t *sequence,
                                   uint32_t ssrc) const;

 private:
  void SendRtpPacket(const EncodedFrame &frame, const uint8_t *payload,
                     uint32_t size, bool marker, uint16_t *sequence,
                     uint32_t ssrc, std::vector<RtpPacket> *packets) const;
  void PacketizeH264(const EncodedFrame &frame, const uint8_t *payload,
                     uint32_t size, uint16_t *sequence, uint32_t ssrc,
                     std::vector<RtpPacket> *packets) const;
  void PacketizeH265(const EncodedFrame &frame, const uint8_t *payload,
                     uint32_t size, uint16_t *sequence, uint32_t ssrc,
                     std::vector<RtpPacket> *packets) const;

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
                                 const std::string &avcc_sample);

std::string BuildTsSegmentHeader(TsMuxerState *state);

void AppendH264AccessUnitToTsSegment(const std::string &access_unit,
                                     int64_t pts_us, int64_t dts_us,
                                     TsMuxerState *state,
                                     std::string *segment_body);

}  // namespace stream_mux
}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_MUX_H_
