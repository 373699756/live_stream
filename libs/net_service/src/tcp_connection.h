#ifndef LIVE_STREAM_NET_SERVICE_SRC_TCP_CONNECTION_H_
#define LIVE_STREAM_NET_SERVICE_SRC_TCP_CONNECTION_H_

#include "event_loop.h"
#include "fd.h"
#include "net_service.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace live_stream {
namespace net_internal {

class NetEngineImpl;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
  TcpConnection(NetEngineImpl *engine, std::shared_ptr<EventLoop> loop, int fd,
                ConnectionId id, const TcpListenOptions &options,
                TcpCallbacks callbacks, NetAddress local, NetAddress peer);
  ~TcpConnection();

  bool Start();
  bool Send(const uint8_t *data, size_t size);
  bool Close();
  bool CloseAfterSend();
  uint32_t PendingBytes() const;
  ConnectionId id() const { return id_; }
  NetAddress peer() const { return peer_; }

private:
  struct OutBuffer {
    std::vector<uint8_t> data;
    size_t offset = 0;
    int64_t enqueue_ms = 0;
  };

  void HandleEvents(uint32_t events);
  void HandleRead();
  void HandleWrite();
  void EnableWrite();
  void DisableWrite();
  void CloseInLoop();
  bool IsSendStalledLocked() const;

  NetEngineImpl *engine_ = nullptr;
  std::shared_ptr<EventLoop> loop_;
  UniqueFd fd_;
  ConnectionId id_ = 0;
  TcpListenOptions options_;
  TcpCallbacks callbacks_;
  NetAddress local_;
  NetAddress peer_;
  mutable std::mutex mutex_;
  std::deque<OutBuffer> send_queue_;
  uint32_t pending_bytes_ = 0;
  bool closed_ = false;
  bool close_after_send_ = false;
};

} // namespace net_internal
} // namespace live_stream

#endif // LIVE_STREAM_NET_SERVICE_SRC_TCP_CONNECTION_H_
