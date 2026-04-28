#include "tcp_connection.h"

#include "infra/time.h"
#include "net_engine_impl.h"

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

TcpConnection::TcpConnection(NetEngineImpl* engine,
                             std::shared_ptr<EventLoop> loop,
                             int fd,
                             ConnectionId id,
                             const TcpListenOptions& options,
                             TcpCallbacks callbacks,
                             NetAddress local,
                             NetAddress peer)
    : engine_(engine),
      loop_(std::move(loop)),
      fd_(fd),
      id_(id),
      options_(options),
      callbacks_(callbacks),
      local_(std::move(local)),
      peer_(std::move(peer)) {}

TcpConnection::~TcpConnection() { fd_.Reset(); }

infra::Status TcpConnection::Start() {
  std::weak_ptr<TcpConnection> weak_self = shared_from_this();
  return loop_->AddFd(fd_.get(), EPOLLIN, [weak_self](uint32_t events) {
    auto self = weak_self.lock();
    if (self) {
      self->HandleEvents(events);
    }
  });
}

infra::Status TcpConnection::Send(const uint8_t* data, size_t size) {
  if (data == nullptr && size > 0) {
    return infra::Status::kInvalidParam;
  }
  if (size == 0) {
    return infra::Status::kOk;
  }
  bool close_slow = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || close_after_send_) {
      return infra::Status::kBusy;
    }
    if (send_queue_.size() >= options_.send_queue_capacity ||
        size > options_.send_buffer_limit_bytes - pending_bytes_) {
      engine_->AddSendBusy();
      close_slow = IsSendStalledLocked();
      if (close_slow) {
        engine_->AddSlowClose();
      } else {
        return infra::Status::kBusy;
      }
    }
    if (!close_slow) {
      OutBuffer buffer;
      buffer.data.assign(data, data + size);
      buffer.enqueue_ms = infra::Time::MonotonicMillis();
      pending_bytes_ += static_cast<uint32_t>(buffer.data.size());
      send_queue_.push_back(std::move(buffer));
    }
  }
  if (close_slow) {
    (void)Close();
    return infra::Status::kBusy;
  }
  std::weak_ptr<TcpConnection> weak_self = shared_from_this();
  const infra::Status status = loop_->Post([weak_self]() {
    auto self = weak_self.lock();
    if (!self) {
      return;
    }
    self->EnableWrite();
    self->HandleWrite();
  });
  if (status != infra::Status::kOk) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!send_queue_.empty()) {
      pending_bytes_ -=
          static_cast<uint32_t>(send_queue_.back().data.size());
      send_queue_.pop_back();
    }
  }
  return status;
}

infra::Status TcpConnection::Close() {
  std::weak_ptr<TcpConnection> weak_self = shared_from_this();
  return loop_->Post([weak_self]() {
    auto self = weak_self.lock();
    if (self) {
      self->CloseInLoop();
    }
  });
}

infra::Status TcpConnection::CloseAfterSend() {
  std::weak_ptr<TcpConnection> weak_self = shared_from_this();
  return loop_->Post([weak_self]() {
    auto self = weak_self.lock();
    if (!self) {
      return;
    }
    bool close_now = false;
    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      if (self->closed_) {
        return;
      }
      self->close_after_send_ = true;
      close_now = self->send_queue_.empty();
    }
    if (close_now) {
      self->CloseInLoop();
    } else {
      self->EnableWrite();
    }
  });
}

uint32_t TcpConnection::PendingBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_bytes_;
}

void TcpConnection::HandleEvents(uint32_t events) {
  if ((events & (EPOLLERR | EPOLLHUP)) != 0) {
    CloseInLoop();
    return;
  }
  if ((events & EPOLLIN) != 0) {
    HandleRead();
  }
  if ((events & EPOLLOUT) != 0) {
    HandleWrite();
  }
}

void TcpConnection::HandleRead() {
  uint8_t buffer[kReadBufferSize];
  while (true) {
    const ssize_t n = recv(fd_.get(), buffer, sizeof(buffer), 0);
    if (n > 0) {
      engine_->AddRead(static_cast<size_t>(n));
      engine_->DispatchRead(callbacks_, id_, buffer, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      CloseInLoop();
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    CloseInLoop();
    return;
  }
}

void TcpConnection::HandleWrite() {
  while (true) {
    ssize_t n = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ || send_queue_.empty()) {
        break;
      }
      OutBuffer& current = send_queue_.front();
      const uint8_t* data = current.data.data() + current.offset;
      const size_t remain = current.data.size() - current.offset;
      n = send(fd_.get(), data, remain, MSG_NOSIGNAL);
      if (n > 0) {
        current.offset += static_cast<size_t>(n);
        pending_bytes_ -= static_cast<uint32_t>(n);
        if (current.offset >= current.data.size()) {
          send_queue_.pop_front();
        }
      }
    }
    if (n > 0) {
      engine_->AddWrite(static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      EnableWrite();
      return;
    }
    CloseInLoop();
    return;
  }
  bool close_now = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    close_now = close_after_send_ && send_queue_.empty();
  }
  if (close_now) {
    CloseInLoop();
    return;
  }
  DisableWrite();
}

void TcpConnection::EnableWrite() {
  if (fd_.valid()) {
    (void)loop_->ModifyFd(fd_.get(), EPOLLIN | EPOLLOUT);
  }
}

void TcpConnection::DisableWrite() {
  if (fd_.valid()) {
    (void)loop_->ModifyFd(fd_.get(), EPOLLIN);
  }
}

void TcpConnection::CloseInLoop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
    send_queue_.clear();
    pending_bytes_ = 0;
  }
  if (fd_.valid()) {
    loop_->RemoveFd(fd_.get());
  }
  fd_.Reset();
  engine_->OnConnectionClosed(id_, callbacks_);
}

bool TcpConnection::IsSendStalledLocked() const {
  if (options_.send_stall_timeout_ms == 0 || send_queue_.empty()) {
    return false;
  }
  const int64_t age_ms =
      infra::Time::MonotonicMillis() - send_queue_.front().enqueue_ms;
  return age_ms >= static_cast<int64_t>(options_.send_stall_timeout_ms);
}

}  // namespace netframe_internal
}  // namespace live_stream
