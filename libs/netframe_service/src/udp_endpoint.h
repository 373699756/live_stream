#ifndef LIVE_STREAM_NETFRAME_SERVICE_SRC_UDP_ENDPOINT_H_
#define LIVE_STREAM_NETFRAME_SERVICE_SRC_UDP_ENDPOINT_H_

#include "event_loop.h"
#include "infra/fd.h"
#include "netframe_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace live_stream {
namespace netframe_internal {

class NetEngineImpl;

class UdpEndpoint : public std::enable_shared_from_this<UdpEndpoint> {
 public:
  UdpEndpoint(NetEngineImpl* engine,
              UdpSocketId id,
              const UdpBindOptions& options,
              const UdpCallbacks& callbacks);
  ~UdpEndpoint();

  infra::Status Start(const std::shared_ptr<EventLoop>& loop);
  void Stop();
  infra::Status SendTo(NetAddress address, const uint8_t* data, size_t size);
  NetAddress LocalAddress() const;

 private:
  void HandleRead();

  NetEngineImpl* engine_ = nullptr;
  UdpSocketId id_ = 0;
  UdpBindOptions options_;
  UdpCallbacks callbacks_;
  std::shared_ptr<EventLoop> loop_;
  mutable std::mutex mutex_;
  infra::UniqueFd fd_;
  NetAddress local_;
  bool running_ = false;
};

}  // namespace netframe_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NETFRAME_SERVICE_SRC_UDP_ENDPOINT_H_
