#include "udp_endpoint.h"

#include "net_engine_impl.h"
#include "socket_util.h"

#include "infra/log.h"

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace live_stream {
namespace net_internal {
namespace {

constexpr uint32_t kReadBufferSize = 4096;
constexpr const char *kModuleName = "net";

}  // namespace

UdpEndpoint::UdpEndpoint(NetEngineImpl *engine, UdpSocketId id,
                         const UdpBindOptions &options,
                         const UdpCallbacks &callbacks)
    : engine_(engine), id_(id), options_(options), callbacks_(callbacks) {}

UdpEndpoint::~UdpEndpoint() { Stop(); }

bool UdpEndpoint::Start(const std::shared_ptr<EventLoop> &loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    sockaddr_in addr = ToSockAddr(options_.address);
    if (addr.sin_family != AF_INET) {
        Error(kModuleName, "UDP bind invalid address ip=%s port=%u",
                        options_.address.ip.c_str(),
                        static_cast<unsigned>(options_.address.port));
        return false;
    }
    UniqueFd fd(CreateSocket(AF_INET, SOCK_DGRAM, 0));
    if (!fd.valid()) {
        const int error = errno;
        Error(kModuleName,
                        "UDP socket failed ip=%s port=%u errno=%d (%s)",
                        options_.address.ip.c_str(),
                        static_cast<unsigned>(options_.address.port), error,
                        ErrnoText(error));
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
        const int error = errno;
        Error(kModuleName,
                        "UDP nonblock failed ip=%s port=%u errno=%d (%s)",
                        options_.address.ip.c_str(),
                        static_cast<unsigned>(options_.address.port), error,
                        ErrnoText(error));
        return false;
    }
    if (bind(fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) !=
        0) {
        const int error = errno;
        Error(kModuleName,
                        "UDP bind failed ip=%s port=%u errno=%d (%s)",
                        options_.address.ip.c_str(),
                        static_cast<unsigned>(options_.address.port), error,
                        ErrnoText(error));
        return false;
    }
    NetAddress local = GetSocketAddress(fd.get(), false);
    if (local.port == 0) {
        Error(kModuleName,
                        "UDP local address unavailable ip=%s port=%u",
                        options_.address.ip.c_str(),
                        static_cast<unsigned>(options_.address.port));
        return false;
    }
    loop_ = loop;
    fd_ = std::move(fd);
    local_ = local;
    running_ = true;
    std::weak_ptr<UdpEndpoint> weak_self = shared_from_this();
    if (!loop_->AddFd(fd_.get(), EPOLLIN, [weak_self](uint32_t events) {
        auto self = weak_self.lock();
        if (self && (events & EPOLLIN) != 0) {
            self->HandleRead();
        }
    })) {
        Error(kModuleName,
                        "UDP epoll add failed ip=%s port=%u local=%s:%u",
                        options_.address.ip.c_str(),
                        static_cast<unsigned>(options_.address.port),
                        local.ip.c_str(), static_cast<unsigned>(local.port));
        running_ = false;
        fd_.Reset();
        loop_.reset();
        return false;
    }
    Info(kModuleName, "UDP bound ip=%s port=%u local=%s:%u",
                   options_.address.ip.c_str(),
                   static_cast<unsigned>(options_.address.port),
                   local.ip.c_str(), static_cast<unsigned>(local.port));
    return true;
}

void UdpEndpoint::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    if (loop_ && fd_.valid()) {
        loop_->RemoveFd(fd_.get());
    }
    fd_.Reset();
}

bool UdpEndpoint::SendTo(NetAddress address, const uint8_t *data, size_t size) {
    NetBufferSlices slices;
    if (!slices.Add(data, size)) {
        return false;
    }
    return SendToSlices(std::move(address), slices);
}

bool UdpEndpoint::SendToSlices(NetAddress address,
                               const NetBufferSlices &slices) {
    if (slices.count > kMaxNetBufferSlices) {
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
    iovec iov[kMaxNetBufferSlices];
    size_t iov_count = 0;
    for (size_t i = 0; i < slices.count; ++i) {
        const NetBufferSlice &slice = slices.slices[i];
        if (slice.size == 0) {
            continue;
        }
        if (slice.data == nullptr) {
            return false;
        }
        iov[iov_count].iov_base = const_cast<uint8_t *>(slice.data);
        iov[iov_count].iov_len = slice.size;
        ++iov_count;
    }
    if (iov_count == 0) {
        return true;
    }
    msghdr message{};
    message.msg_name = &addr;
    message.msg_namelen = sizeof(addr);
    message.msg_iov = iov;
    message.msg_iovlen = iov_count;
    const ssize_t ret = sendmsg(fd, &message, 0);
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
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0,
                                   reinterpret_cast<sockaddr *>(&peer), &peer_len);
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

}  // namespace net_internal
}  // namespace live_stream
