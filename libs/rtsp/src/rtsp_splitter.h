#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SPLITTER_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SPLITTER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

enum class RtspSplitterStatus {
  kOk,
  kNeedMoreData,
  kPayloadTooLarge,
};

struct RtspInterleavedPacket {
  uint8_t channel = 0;
  std::string payload;
};

struct RtspSplitterResult {
  RtspSplitterStatus status = RtspSplitterStatus::kOk;
  std::vector<std::string> requests;
  std::vector<RtspInterleavedPacket> interleaved_packets;
};

class RtspSplitter {
 public:
  bool Append(const uint8_t *data, uint32_t size);
  RtspSplitterResult Split(uint32_t max_request_bytes);
  void Clear();
  size_t BufferedBytes() const;

 private:
  std::string recv_buffer_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SPLITTER_H_
