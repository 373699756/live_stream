#ifndef LIVE_STREAM_NETFRAME_SERVICE_SRC_TCP_SERVER_H_
#define LIVE_STREAM_NETFRAME_SERVICE_SRC_TCP_SERVER_H_

#include "event_loop.h"
#include "fd.h"
#include "netframe_service.h"

#include <memory>
#include <mutex>

namespace live_stream {
namespace netframe_internal {

class NetEngineImpl;

class TcpServer : public std::enable_shared_from_this<TcpServer> {
 public:
  TcpServer(NetEngineImpl* engine,
            TcpServerId id,
            const TcpListenOptions& options,
            const TcpCallbacks& callbacks);
  ~TcpServer();

  bool Start(const std::shared_ptr<EventLoop>& loop);
  void Stop();
  NetAddress LocalAddress() const;

 private:
  void AcceptLoop();

  NetEngineImpl* engine_ = nullptr;
  TcpServerId id_ = 0;
  TcpListenOptions options_;
  TcpCallbacks callbacks_;
  std::shared_ptr<EventLoop> loop_;
  mutable std::mutex mutex_;
  UniqueFd listen_fd_;
  NetAddress local_;
  bool running_ = false;
};

}  // namespace netframe_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NETFRAME_SERVICE_SRC_TCP_SERVER_H_
