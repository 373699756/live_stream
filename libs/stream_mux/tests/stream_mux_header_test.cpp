#include "stream_mux.h"

#include "media/media_buffer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

class CountingRtpSink : public live_stream::stream_mux::IRtpPacketSink {
public:
  bool OnRtpPacket(
      const live_stream::stream_mux::RtpPacketView& packet) override {
    ++packet_count;
    total_bytes += packet.Size();
    last_marker = packet.marker;
    return packet.slice_count != 0;
  }

  int packet_count = 0;
  size_t total_bytes = 0;
  bool last_marker = false;
};

}  // namespace

int main() {
  std::string flv_header = live_stream::stream_mux::BuildFlvFileHeader();
  if (flv_header.size() < 13 || flv_header[0] != 'F' ||
      flv_header[1] != 'L' || flv_header[2] != 'V') {
    return 1;
  }

  live_stream::stream_mux::TsMuxerState ts_state;
  std::string ts_header = live_stream::stream_mux::BuildTsSegmentHeader(
      live_stream::VideoCodec::kH264, &ts_state);
  if (ts_header.size() != 376 ||
      static_cast<uint8_t>(ts_header[0]) != 0x47 ||
      static_cast<uint8_t>(ts_header[188]) != 0x47) {
    return 2;
  }

  const uint8_t h264[] = {
      0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84,
  };
  live_stream::EncodedFrame frame;
  frame.codec = live_stream::VideoCodec::kH264;
  frame.frame_type = live_stream::FrameType::kIdr;
  frame.pts_us = 90000;
  frame.dts_us = 90000;
  frame.buffer = live_stream::VideoBufferAlloc(sizeof(h264));
  if (frame.buffer == nullptr) {
    return 3;
  }
  for (size_t i = 0; i < sizeof(h264); ++i) {
    frame.buffer->data[i] = h264[i];
  }
  if (!live_stream::VideoBufferSetSize(frame.buffer, sizeof(h264))) {
    return 4;
  }
  frame.size = sizeof(h264);

  uint16_t sequence = 7;
  CountingRtpSink sink;
  live_stream::stream_mux::RtpPacketizer packetizer(1200);
  if (!packetizer.Packetize(frame, &sequence, 0x11223344, &sink)) {
    return 5;
  }
  if (sink.packet_count != 1 || sink.total_bytes == 0 || !sink.last_marker ||
      sequence != 8) {
    return 6;
  }

  return 0;
}
