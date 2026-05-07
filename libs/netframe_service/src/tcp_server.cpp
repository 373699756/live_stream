#include "tcp_server.h"

#include "net_engine_impl.h"
#include "socket_util.h"
#include "tcp_connection.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <cerrno>
#include <utility>

namespace live_stream {
namespace netframe_internal {

TcpServer::TcpServer(NetEngineImpl* engine,
                     TcpServerId id,
                     const TcpListenOptions& options,
                     const TcpCallbacks& callbacks)
    : engine_(engine), id_(id), options_(options), callbacks_(callbacks) {}

TcpServer::~TcpServer() { Stop(); }

bool TcpServer::Start(const std::shared_ptr<EventLoop>& loop) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    return true;
  }
  if (!loop || options_.backlog == 0 || options_.max_connections == 0 ||
      options_.send_queue_capacity == 0 ||
      options_.send_buffer_limit_bytes == 0) {
    return false;
  }
  sockaddr_in addr = ToSockAddr(options_.address);
  if (addr.sin_family != AF_INET) {
    return false;
  }
  UniqueFd fd(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid()) {
    return false;
  }
  int enabled = 1;
  (void)setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &enabled,
                   sizeof(enabled));
  if (options_.reuse_port) {
#ifdef SO_REUSEPORT
    (void)setsockopt(fd.get(), SOL_SOCKET, SO_REUSEPORT, &enabled,
                     sizeof(enabled));
#endif
  }
  if (!SetNonBlocking(fd.get())) {
    return false;
  }
  if (bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    return false;
  }
  if (listen(fd.get(), static_cast<int>(options_.backlog)) != 0) {
    return false;
  }
  NetAddress local = GetSocketAddress(fd.get(), false);
  if (local.port == 0) {
    return false;
  }
  loop_ = loop;
  listen_fd_ = std::move(fd);
  local_ = local;
  running_ = true;
  std::weak_ptr<TcpServer> weak_self = shared_from_this();
  return loop_->AddFd(listen_fd_.get(), EPOLLIN, [weak_self](uint32_t events) {
    auto self = weak_self.lock();
    if (self && (events & EPOLLIN) != 0) {
      self->AcceptLoop();
    }
  });
}

void TcpServer::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  if (loop_ && listen_fd_.valid()) {
    loop_->RemoveFd(listen_fd_.get());
  }
  listen_fd_.Reset();
}

NetAddress TcpServer::LocalAddress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return local_;
}

void TcpServer::AcceptLoop() {
  while (true) {
    int listen_fd = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_ || !listen_fd_.valid()) {
        return;
      }
      listen_fd = listen_fd_.get();
    }
    sockaddr_in peer_addr {};
    socklen_t peer_len = sizeof(peer_addr);
    const int fd = accept4(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr),
                           &peer_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      return;
    }
    UniqueFd accepted(fd);
    if (!engine_->CanAccept(options_.max_connections)) {
      engine_->AddRejected();
      continue;
    }
    int enabled = 1;
    if (options_.tcp_no_delay) {
      (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled,
                       sizeof(enabled));
    }
    if (options_.keepalive) {
      (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled,
                       sizeof(enabled));
    }
    NetAddress local = GetSocketAddress(fd, false);
    if (local.port == 0) {
      engine_->AddRejected();
      continue;
    }
    const ConnectionId id = engine_->AllocateConnectionId();
    auto connection = std::make_shared<TcpConnection>(
        engine_, engine_->NextLoop(), accepted.Release(), id, options_,
        callbacks_, local, FromSockAddr(peer_addr));
    if (!connection->Start()) {
      engine_->AddRejected();
      continue;
    }
    engine_->RegisterConnection(connection);
    engine_->AddAccepted();
    engine_->DispatchAccept(callbacks_, id, connection->peer());
  }
}

}  // namespace netframe_internal
}  // namespace live_stream
