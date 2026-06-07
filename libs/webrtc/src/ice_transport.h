#ifndef LIVE_STREAM_WEBRTC_SRC_ICE_TRANSPORT_H_
#define LIVE_STREAM_WEBRTC_SRC_ICE_TRANSPORT_H_

#include "net.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {
namespace webrtc_internal {

struct IceSelectedPair {
  NetAddress local;
  NetAddress remote;
  uint32_t priority = 0;
  bool nominated = false;
};

class IceTransport {
public:
  explicit IceTransport(std::string peer_id);
  ~IceTransport();

  IceTransport(const IceTransport &) = delete;
  IceTransport &operator=(const IceTransport &) = delete;

  bool Start(NetEngine *net_engine, const UdpCallbacks &callbacks,
             const std::string &listen_ip, uint16_t port,
             std::string local_ufrag, std::string local_password);
  void Stop();

  bool HandleUdpPacket(NetAddress peer, const uint8_t *data, size_t size,
                       bool *connected_now);
  bool SendToSelected(const uint8_t *data, size_t size);

  bool started() const { return socket_id_ != 0; }
  bool connected() const { return selected_pair_.remote.port != 0; }
  UdpSocketId socket_id() const { return socket_id_; }
  NetAddress local_address() const { return local_address_; }
  const std::string &local_ufrag() const { return local_ufrag_; }
  const std::string &local_password() const { return local_password_; }
  bool selected_pair(IceSelectedPair *pair) const;

private:
  std::string peer_id_;
  NetEngine *net_engine_ = nullptr;
  UdpSocketId socket_id_ = 0;
  NetAddress local_address_;
  std::string local_ufrag_;
  std::string local_password_;
  IceSelectedPair selected_pair_;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_ICE_TRANSPORT_H_
