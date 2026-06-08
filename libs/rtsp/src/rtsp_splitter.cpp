#include "rtsp_splitter.h"

namespace live_stream {

bool RtspSplitter::Append(const uint8_t *data, uint32_t size) {
  if (data == nullptr) {
    return false;
  }
  recv_buffer_.append(reinterpret_cast<const char *>(data), size);
  return true;
}

RtspSplitterResult RtspSplitter::Split(uint32_t max_request_bytes) {
  RtspSplitterResult result;
  while (!recv_buffer_.empty()) {
    if (recv_buffer_[0] == '$') {
      // TCP interleaved frame 与 RTSP request 共用控制连接；'$' 后面是 channel
      // 和 16bit payload length，必须完整收到一个 frame 后才能交给上层。
      if (recv_buffer_.size() < 4) {
        result.status = RtspSplitterStatus::kNeedMoreData;
        return result;
      }
      const uint8_t channel = static_cast<uint8_t>(recv_buffer_[1]);
      const uint16_t payload_size =
          (static_cast<uint8_t>(recv_buffer_[2]) << 8) |
          static_cast<uint8_t>(recv_buffer_[3]);
      if (recv_buffer_.size() < 4U + payload_size) {
        result.status = RtspSplitterStatus::kNeedMoreData;
        return result;
      }
      RtspInterleavedPacket packet;
      packet.channel = channel;
      packet.payload = recv_buffer_.substr(4, payload_size);
      result.interleaved_packets.push_back(std::move(packet));
      recv_buffer_.erase(0, 4U + payload_size);
      continue;
    }

    // 这里只支持当前控制命令使用的 header-only RTSP request；如果未来支持
    // 带 body 方法，需要同步扩展 Content-Length 处理。
    const size_t end = recv_buffer_.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (recv_buffer_.size() > max_request_bytes) {
        result.status = RtspSplitterStatus::kPayloadTooLarge;
      } else {
        result.status = RtspSplitterStatus::kNeedMoreData;
      }
      return result;
    }

    const size_t request_size = end + 4;
    if (request_size > max_request_bytes) {
      result.status = RtspSplitterStatus::kPayloadTooLarge;
      return result;
    }
    result.requests.push_back(recv_buffer_.substr(0, request_size));
    recv_buffer_.erase(0, request_size);
  }
  return result;
}

void RtspSplitter::Clear() {
  recv_buffer_.clear();
}

size_t RtspSplitter::BufferedBytes() const {
  return recv_buffer_.size();
}

}  // namespace live_stream
