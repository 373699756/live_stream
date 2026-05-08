#ifndef LIVE_STREAM_NET_SERVICE_H_
#define LIVE_STREAM_NET_SERVICE_H_

#include "infra/executor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

struct NetAddress {
  std::string ip = "0.0.0.0";
  uint16_t port = 0;
};

using ConnectionId = uint64_t;
using TcpServerId = uint64_t;
using UdpSocketId = uint64_t;
using NetTimerId = uint64_t;

enum class CallbackMode {
  kInlineOnIo,
  kPostToExecutor,
};

struct NetEngineOptions {
  uint32_t io_threads = 1;
  uint32_t max_events_per_loop = 64;
  uint32_t task_queue_capacity = 4096;
  CallbackMode callback_mode = CallbackMode::kInlineOnIo;
  infra::Executor *callback_executor = nullptr;
};

struct TcpListenOptions {
  NetAddress address;
  uint32_t backlog = 128;
  uint32_t max_connections = 64;
  uint32_t send_queue_capacity = 128;
  uint32_t send_buffer_limit_bytes = 1024 * 1024;
  uint32_t send_stall_timeout_ms = 0;
  bool reuse_port = false;
  bool tcp_no_delay = false;
  bool keepalive = false;
};

struct UdpBindOptions {
  NetAddress address;
  uint32_t recv_buffer_bytes = 0;
  uint32_t send_buffer_bytes = 0;
};

using TcpAcceptFn = void (*)(void *user, ConnectionId id, NetAddress peer);
using TcpReadFn = void (*)(void *user, ConnectionId id, const uint8_t *data,
                           size_t size);
using TcpCloseFn = void (*)(void *user, ConnectionId id);
using UdpReadFn = void (*)(void *user, UdpSocketId socket_id, NetAddress peer,
                           const uint8_t *data, size_t size);

struct TcpCallbacks {
  void *user = nullptr;
  TcpAcceptFn on_accept = nullptr;
  TcpReadFn on_read = nullptr;
  TcpCloseFn on_close = nullptr;
};

struct UdpCallbacks {
  void *user = nullptr;
  UdpReadFn on_read = nullptr;
};

struct NetStats {
  uint32_t active_connections = 0;
  uint64_t accepted_connections = 0;
  uint64_t rejected_connections = 0;
  uint64_t closed_connections = 0;
  uint64_t read_bytes = 0;
  uint64_t written_bytes = 0;
  uint64_t sent_datagrams = 0;
  uint64_t received_datagrams = 0;
  uint64_t send_busy_count = 0;
  uint64_t slow_client_closes = 0;
};

class NetEngine {
public:
  virtual ~NetEngine() = default;

  virtual bool Start() = 0;
  virtual void Stop() = 0;

  virtual TcpServerId ListenTcp(const TcpListenOptions &options,
                                const TcpCallbacks &callbacks) = 0;
  virtual bool CloseTcp(TcpServerId id) = 0;
  virtual UdpSocketId BindUdp(const UdpBindOptions &options,
                              const UdpCallbacks &callbacks) = 0;
  virtual bool CloseUdp(UdpSocketId id) = 0;

  virtual bool Send(ConnectionId id, const uint8_t *data, size_t size) = 0;
  virtual bool Close(ConnectionId id) = 0;
  virtual bool CloseAfterSend(ConnectionId id) = 0;
  virtual bool SendTo(UdpSocketId id, NetAddress address, const uint8_t *data,
                      size_t size) = 0;

  virtual NetTimerId RunOnIoAfter(uint32_t delay_ms, infra::Task task) = 0;
  virtual bool CancelIoTimer(NetTimerId id) = 0;

  virtual NetAddress TcpLocalAddress(TcpServerId id) const = 0;
  virtual NetAddress UdpLocalAddress(UdpSocketId id) const = 0;
  virtual uint32_t PendingBytes(ConnectionId id) const = 0;
  virtual NetStats GetStats() const = 0;
};

std::unique_ptr<NetEngine> CreateNetEngine(const NetEngineOptions &options);

} // namespace live_stream

#endif // LIVE_STREAM_NET_SERVICE_H_
