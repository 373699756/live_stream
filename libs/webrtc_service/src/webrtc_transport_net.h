#ifndef LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_TRANSPORT_NET_H_
#define LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_TRANSPORT_NET_H_

#include "net_service.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {
namespace webrtc_internal {

class NetWebrtcTransport {
public:
  explicit NetWebrtcTransport(NetEngine *net_engine);
  ~NetWebrtcTransport();

  bool Start(const std::string &listen_ip, uint16_t port);
  void Stop();
  bool SendTo(NetAddress address, const uint8_t *data, size_t size);
  bool started() const { return socket_id_ != 0; }

private:
  NetEngine *net_engine_ = nullptr;
  UdpSocketId socket_id_ = 0;
};

} // namespace webrtc_internal
} // namespace live_stream

#endif // LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_TRANSPORT_NET_H_
