#include "net_engine_impl.h"

#include "tcp_connection.h"
#include "tcp_server.h"
#include "udp_endpoint.h"

#include <utility>
#include <vector>

namespace live_stream {
namespace netframe_internal {

NetEngineImpl::NetEngineImpl(const NetEngineOptions& options)
    : options_(options) {}

NetEngineImpl::~NetEngineImpl() { Stop(); }

infra::Status NetEngineImpl::Start() {
  if (options_.io_threads == 0) {
    return infra::Status::kInvalidParam;
  }
  if (options_.callback_mode == CallbackMode::kPostToExecutor &&
      options_.callback_executor == nullptr) {
    return infra::Status::kInvalidParam;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
      return infra::Status::kOk;
    }
    if (loops_.empty()) {
      for (uint32_t i = 0; i < options_.io_threads; ++i) {
        loops_.push_back(std::make_shared<EventLoop>(
            options_.max_events_per_loop, options_.task_queue_capacity));
      }
    }
  }
  for (const auto& loop : loops_) {
    const infra::Status status = loop->Start();
    if (status != infra::Status::kOk) {
      Stop();
      return status;
    }
  }
  running_ = true;
  for (auto& entry : servers_) {
    const infra::Status status = entry.second->Start(NextLoop());
    if (status != infra::Status::kOk) {
      Stop();
      return status;
    }
  }
  for (auto& entry : udp_sockets_) {
    const infra::Status status = entry.second->Start(NextLoop());
    if (status != infra::Status::kOk) {
      Stop();
      return status;
    }
  }
  return infra::Status::kOk;
}

void NetEngineImpl::Stop() {
  running_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : servers_) {
      entry.second->Stop();
    }
    for (auto& entry : udp_sockets_) {
      entry.second->Stop();
    }
    auto connections = connections_;
    for (auto& entry : connections) {
      (void)entry.second->Close();
    }
  }
  for (const auto& loop : loops_) {
    loop->Stop();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  connections_.clear();
}

infra::Result<TcpServerId> NetEngineImpl::ListenTcp(
    const TcpListenOptions& options, const TcpCallbacks& callbacks) {
  if (callbacks.on_read == nullptr && callbacks.on_accept == nullptr) {
    return infra::Result<TcpServerId>::Fail(infra::Status::kInvalidParam);
  }
  const TcpServerId id = next_server_id_++;
  auto server = std::make_shared<TcpServer>(this, id, options, callbacks);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    servers_[id] = server;
  }
  if (running_) {
    const infra::Status status = server->Start(NextLoop());
    if (status != infra::Status::kOk) {
      std::lock_guard<std::mutex> lock(mutex_);
      servers_.erase(id);
      return infra::Result<TcpServerId>::Fail(status);
    }
  }
  return infra::Result<TcpServerId>::Ok(id);
}

infra::Result<UdpSocketId> NetEngineImpl::BindUdp(
    const UdpBindOptions& options, const UdpCallbacks& callbacks) {
  const UdpSocketId id = next_udp_id_++;
  auto socket = std::make_shared<UdpEndpoint>(this, id, options, callbacks);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    udp_sockets_[id] = socket;
  }
  if (running_) {
    const infra::Status status = socket->Start(NextLoop());
    if (status != infra::Status::kOk) {
      std::lock_guard<std::mutex> lock(mutex_);
      udp_sockets_.erase(id);
      return infra::Result<UdpSocketId>::Fail(status);
    }
  }
  return infra::Result<UdpSocketId>::Ok(id);
}

infra::Status NetEngineImpl::CloseTcp(TcpServerId id) {
  std::shared_ptr<TcpServer> server;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = servers_.find(id);
    if (it == servers_.end()) {
      return infra::Status::kNotFound;
    }
    server = it->second;
    servers_.erase(it);
  }
  server->Stop();
  return infra::Status::kOk;
}

infra::Status NetEngineImpl::CloseUdp(UdpSocketId id) {
  std::shared_ptr<UdpEndpoint> socket;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = udp_sockets_.find(id);
    if (it == udp_sockets_.end()) {
      return infra::Status::kNotFound;
    }
    socket = it->second;
    udp_sockets_.erase(it);
  }
  socket->Stop();
  return infra::Status::kOk;
}

infra::Status NetEngineImpl::Send(ConnectionId id,
                                  const uint8_t* data,
                                  size_t size) {
  auto connection = FindConnection(id);
  return connection ? connection->Send(data, size) : infra::Status::kNotFound;
}

infra::Status NetEngineImpl::Close(ConnectionId id) {
  auto connection = FindConnection(id);
  return connection ? connection->Close() : infra::Status::kNotFound;
}

infra::Status NetEngineImpl::CloseAfterSend(ConnectionId id) {
  auto connection = FindConnection(id);
  return connection ? connection->CloseAfterSend()
                    : infra::Status::kNotFound;
}

infra::Status NetEngineImpl::SendTo(UdpSocketId id,
                                    NetAddress address,
                                    const uint8_t* data,
                                    size_t size) {
  std::shared_ptr<UdpEndpoint> socket;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = udp_sockets_.find(id);
    if (it == udp_sockets_.end()) {
      return infra::Status::kNotFound;
    }
    socket = it->second;
  }
  return socket->SendTo(std::move(address), data, size);
}

infra::Result<NetTimerId> NetEngineImpl::RunOnIoAfter(uint32_t delay_ms,
                                                      infra::Task task) {
  auto loop = NextLoop();
  if (!loop) {
    return infra::Result<NetTimerId>::Fail(infra::Status::kBusy);
  }
  return loop->RunAfter(delay_ms, std::move(task));
}

infra::Status NetEngineImpl::CancelIoTimer(NetTimerId id) {
  for (const auto& loop : loops_) {
    const infra::Status status = loop->CancelTimer(id);
    if (status == infra::Status::kOk) {
      return status;
    }
  }
  return infra::Status::kNotFound;
}

infra::Result<NetAddress> NetEngineImpl::TcpLocalAddress(
    TcpServerId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = servers_.find(id);
  if (it == servers_.end()) {
    return infra::Result<NetAddress>::Fail(infra::Status::kNotFound);
  }
  return infra::Result<NetAddress>::Ok(it->second->LocalAddress());
}

infra::Result<NetAddress> NetEngineImpl::UdpLocalAddress(
    UdpSocketId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = udp_sockets_.find(id);
  if (it == udp_sockets_.end()) {
    return infra::Result<NetAddress>::Fail(infra::Status::kNotFound);
  }
  return infra::Result<NetAddress>::Ok(it->second->LocalAddress());
}

uint32_t NetEngineImpl::PendingBytes(ConnectionId id) const {
  auto connection = FindConnection(id);
  return connection ? connection->PendingBytes() : 0;
}

NetStats NetEngineImpl::GetStats() const {
  NetStats stats;
  uint32_t active_connections = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_connections = static_cast<uint32_t>(connections_.size());
  }
  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats = stats_;
  }
  stats.active_connections = active_connections;
  return stats;
}

void NetEngineImpl::AddAccepted() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ++stats_.accepted_connections;
}

void NetEngineImpl::AddRejected() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ++stats_.rejected_connections;
}

void NetEngineImpl::AddRead(size_t size) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.read_bytes += size;
}

void NetEngineImpl::AddWrite(size_t size) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.written_bytes += size;
}

void NetEngineImpl::AddSendBusy() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ++stats_.send_busy_count;
}

void NetEngineImpl::AddSlowClose() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ++stats_.slow_client_closes;
}

void NetEngineImpl::AddUdpRx() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ++stats_.received_datagrams;
}

void NetEngineImpl::AddUdpTx() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ++stats_.sent_datagrams;
}

bool NetEngineImpl::CanAccept(uint32_t max_connections) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.size() < max_connections;
}

void NetEngineImpl::RegisterConnection(
    const std::shared_ptr<TcpConnection>& connection) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_[connection->id()] = connection;
}

void NetEngineImpl::OnConnectionClosed(ConnectionId id,
                                       const TcpCallbacks& callbacks) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(id);
  }
  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.closed_connections;
  }
  DispatchClose(callbacks, id);
}

void NetEngineImpl::DispatchAccept(const TcpCallbacks& callbacks,
                                   ConnectionId id,
                                   NetAddress peer) {
  if (callbacks.on_accept == nullptr) {
    return;
  }
  if (options_.callback_mode == CallbackMode::kPostToExecutor) {
    (void)options_.callback_executor->Post(
        [callbacks, id, peer = std::move(peer)]() mutable {
          callbacks.on_accept(callbacks.user, id, peer);
        });
    return;
  }
  callbacks.on_accept(callbacks.user, id, std::move(peer));
}

void NetEngineImpl::DispatchRead(const TcpCallbacks& callbacks,
                                 ConnectionId id,
                                 const uint8_t* data,
                                 size_t size) {
  if (callbacks.on_read == nullptr) {
    return;
  }
  if (options_.callback_mode == CallbackMode::kPostToExecutor) {
    std::vector<uint8_t> copy(data, data + size);
    (void)options_.callback_executor->Post(
        [callbacks, id, copy = std::move(copy)]() mutable {
          callbacks.on_read(callbacks.user, id, copy.data(), copy.size());
        });
    return;
  }
  callbacks.on_read(callbacks.user, id, data, size);
}

void NetEngineImpl::DispatchClose(const TcpCallbacks& callbacks,
                                  ConnectionId id) {
  if (callbacks.on_close == nullptr) {
    return;
  }
  if (options_.callback_mode == CallbackMode::kPostToExecutor) {
    (void)options_.callback_executor->Post([callbacks, id]() {
      callbacks.on_close(callbacks.user, id);
    });
    return;
  }
  callbacks.on_close(callbacks.user, id);
}

void NetEngineImpl::DispatchUdp(const UdpCallbacks& callbacks,
                                UdpSocketId socket_id,
                                NetAddress peer,
                                const uint8_t* data,
                                size_t size) {
  if (callbacks.on_read == nullptr) {
    return;
  }
  if (options_.callback_mode == CallbackMode::kPostToExecutor) {
    std::vector<uint8_t> copy(data, data + size);
    (void)options_.callback_executor->Post(
        [callbacks, socket_id, peer = std::move(peer),
         copy = std::move(copy)]() mutable {
          callbacks.on_read(callbacks.user, socket_id, peer, copy.data(),
                            copy.size());
        });
    return;
  }
  callbacks.on_read(callbacks.user, socket_id, std::move(peer), data, size);
}

std::shared_ptr<EventLoop> NetEngineImpl::NextLoop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (loops_.empty()) {
    return nullptr;
  }
  const uint32_t index =
      next_loop_.fetch_add(1) % static_cast<uint32_t>(loops_.size());
  return loops_[index];
}

std::shared_ptr<TcpConnection> NetEngineImpl::FindConnection(
    ConnectionId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = connections_.find(id);
  return it == connections_.end() ? nullptr : it->second;
}

}  // namespace netframe_internal
}  // namespace live_stream
