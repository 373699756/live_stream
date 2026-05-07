#include "udp_endpoint.h"

#include "net_engine_impl.h"
#include "socket_util.h"

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace live_stream {
namespace netframe_internal {
namespace {

constexpr uint32_t kReadBufferSize = 4096;

}  // namespace

UdpEndpoint::UdpEndpoint(NetEngineImpl* engine,
                         UdpSocketId id,
                         const UdpBindOptions& options,
                         const UdpCallbacks& callbacks)
    : engine_(engine), id_(id), options_(options), callbacks_(callbacks) {}

UdpEndpoint::~UdpEndpoint() { Stop(); }

bool UdpEndpoint::Start(const std::shared_ptr<EventLoop>& loop) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    return true;
  }
  sockaddr_in addr = ToSockAddr(options_.address);
  if (addr.sin_family != AF_INET) {
    return false;
  }
  UniqueFd fd(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!fd.valid()) {
    return false;
  }
  if (options_.recv_buffer_bytes > 0) {
    const int size = static_cast<int>(options_.recv_buffer_bytes);
    (void)setsockopt(fd.get(), SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
  }
  if (options_.send_buffer_bytes > 0) {
    const int size = static_cast<int>(options_.send_buffer_bytes);
    (void)setsockopt(fd.get(), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
  }
  if (!SetNonBlocking(fd.get())) {
    return false;
  }
  if (bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    return false;
  }
  NetAddress local = GetSocketAddress(fd.get(), false);
  if (local.port == 0) {
    return false;
  }
  loop_ = loop;
  fd_ = std::move(fd);
  local_ = local;
  running_ = true;
  std::weak_ptr<UdpEndpoint> weak_self = shared_from_this();
  return loop_->AddFd(fd_.get(), EPOLLIN, [weak_self](uint32_t events) {
    auto self = weak_self.lock();
    if (self && (events & EPOLLIN) != 0) {
      self->HandleRead();
    }
  });
}

void UdpEndpoint::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  if (loop_ && fd_.valid()) {
    loop_->RemoveFd(fd_.get());
  }
  fd_.Reset();
}

bool UdpEndpoint::SendTo(NetAddress address, const uint8_t* data, size_t size) {
  if (data == nullptr && size > 0) {
    return false;
  }
  sockaddr_in addr = ToSockAddr(address);
  if (addr.sin_family != AF_INET) {
    return false;
  }
  int fd = -1;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !fd_.valid()) {
      return false;
    }
    fd = fd_.get();
  }
  const ssize_t ret = sendto(fd, data, size, 0,
                             reinterpret_cast<const sockaddr*>(&addr),
                             sizeof(addr));
  if (ret < 0) {
    return false;
  }
  engine_->AddUdpTx();
  return true;
}

NetAddress UdpEndpoint::LocalAddress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return local_;
}

void UdpEndpoint::HandleRead() {
  uint8_t buffer[kReadBufferSize];
  while (true) {
    int fd = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_ || !fd_.valid()) {
        return;
      }
      fd = fd_.get();
    }
    sockaddr_in peer {};
    socklen_t peer_len = sizeof(peer);
    const ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0,
                               reinterpret_cast<sockaddr*>(&peer),
                               &peer_len);
    if (n > 0) {
      engine_->AddUdpRx();
      engine_->DispatchUdp(callbacks_, id_, FromSockAddr(peer), buffer,
                           static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    return;
  }
}

}  // namespace netframe_internal
}  // namespace live_stream
