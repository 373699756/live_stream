#include "udp_socket.h"

#include "net_io_impl.h"
#include "socket_util.h"

#include "infra/log.h"

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace live_stream {
namespace net_internal {
namespace {

constexpr uint32_t kReadBufferSize = 4096;
constexpr const char *kModuleName = "net";

}  // namespace

UdpSocket::UdpSocket(NetIoImpl *net_io, UdpSocketId id,
                     const UdpBindOptions &options,
                     const UdpCallbacks &callbacks)
    : net_io_(net_io), id_(id), options_(options), callbacks_(callbacks) {}

UdpSocket::~UdpSocket() { Stop(); }

bool UdpSocket::Start(const std::shared_ptr<EventLoop> &loop) {
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
    std::weak_ptr<UdpSocket> weak_self = shared_from_this();
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

void UdpSocket::Stop() {
    std::shared_ptr<EventLoop> loop;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        loop = loop_;
        fd = fd_.get();
    }
    if (loop && fd >= 0) {
        loop->RemoveFd(fd);
    }
    // UDP endpoint 没有连接级 close callback；上层协议必须在 CloseUdp() 前清理
    // 自己保存的 session/transport 状态。
    std::lock_guard<std::mutex> lock(mutex_);
    fd_.Reset();
    loop_.reset();
}

bool UdpSocket::SendTo(NetAddress address, const uint8_t *data, size_t size) {
    NetBufferSlices slices;
    if (!slices.Add(data, size)) {
        return false;
    }
    return SendToSlices(std::move(address), slices);
}

bool UdpSocket::SendToSlices(NetAddress address,
                             const NetBufferSlices &slices) {
    if (slices.slice_size > kMaxNetBufferSlices) {
        return false;
    }
    std::shared_ptr<EventLoop> loop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop = loop_;
    }
    if (loop != nullptr && loop->IsCurrentThread()) {
        return SendToSlicesInLoop(std::move(address), slices);
    }
    size_t total_size = 0;
    for (size_t i = 0; i < slices.slice_size; ++i) {
        const NetBufferSlice &slice = slices.slices[i];
        if (slice.size == 0) {
            continue;
        }
        if (slice.data == nullptr) {
            return false;
        }
        total_size += slice.size;
    }
    if (total_size == 0) {
        return true;
    }
    auto datagram = std::make_shared<std::vector<uint8_t>>();
    datagram->resize(total_size);
    size_t offset = 0;
    for (size_t i = 0; i < slices.slice_size; ++i) {
        const NetBufferSlice &slice = slices.slices[i];
        if (slice.size == 0) {
            continue;
        }
        std::memcpy(datagram->data() + offset, slice.data, slice.size);
        offset += slice.size;
    }
    std::weak_ptr<UdpSocket> weak_self = shared_from_this();
    return loop != nullptr &&
           loop->Post([weak_self, address = std::move(address),
                       datagram]() mutable {
               auto self = weak_self.lock();
               if (self) {
                   static_cast<void>(self->SendPreparedDatagram(
                       std::move(address), datagram));
               }
           }) == event::EventStatus::kOk;
}

bool UdpSocket::SendPreparedDatagram(
    NetAddress address,
    const std::shared_ptr<std::vector<uint8_t>> &datagram) {
    if (!datagram || datagram->empty()) {
        return false;
    }
    NetBufferSlices slices;
    if (!slices.Add(datagram->data(), datagram->size())) {
        return false;
    }
    return SendToSlicesInLoop(std::move(address), slices);
}

bool UdpSocket::SendToSlicesInLoop(NetAddress address,
                                   const NetBufferSlices &slices) {
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
    // UDP 不维护发送队列，直接用 sendmsg 聚合分片。调用返回后网络层不再持有
    // slice 指针，适合 RTP datagram 和 WS-Discovery 这类逐包发送。
    iovec iov[kMaxNetBufferSlices];
    size_t iov_size = 0;
    for (size_t i = 0; i < slices.slice_size; ++i) {
        const NetBufferSlice &slice = slices.slices[i];
        if (slice.size == 0) {
            continue;
        }
        if (slice.data == nullptr) {
            return false;
        }
        iov[iov_size].iov_base = const_cast<uint8_t *>(slice.data);
        iov[iov_size].iov_len = slice.size;
        ++iov_size;
    }
    if (iov_size == 0) {
        return true;
    }
    msghdr msg{};
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;
    const ssize_t ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        const int error = errno;
        Error(kModuleName,
              "UDP send failed local=%s:%u peer=%s:%u errno=%d (%s)",
              local_.ip.c_str(), static_cast<unsigned>(local_.port),
              address.ip.c_str(), static_cast<unsigned>(address.port), error,
              ErrnoText(error));
        return false;
    }
    net_io_->AddUdpTx();
    return true;
}

bool UdpSocket::SetPeer(NetAddress peer) {
    sockaddr_in addr = ToSockAddr(peer);
    if (addr.sin_family != AF_INET) {
        return false;
    }
    // selected peer 只给 RTP/ICE 这类已协商对端使用；ONVIF discovery 仍走
    // SendTo()，因为每个 Probe 的回复目标不同。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || loop_ == nullptr) {
        return false;
    }
    peer_ = std::move(peer);
    has_peer_ = true;
    return true;
}

bool UdpSocket::SendToPeer(const uint8_t *data, size_t size) {
    NetBufferSlices slices;
    if (!slices.Add(data, size)) {
        return false;
    }
    return SendToPeerSlices(slices);
}

bool UdpSocket::SendToPeerSlices(const NetBufferSlices &slices) {
    NetAddress peer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_peer_) {
            return false;
        }
        peer = peer_;
    }
    return SendToSlices(std::move(peer), slices);
}

NetAddress UdpSocket::LocalAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return local_;
}

NetAddress UdpSocket::PeerAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_peer_ ? peer_ : NetAddress{};
}

void UdpSocket::HandleRead() {
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
            net_io_->AddUdpRx();
            // buffer 是栈内存。DispatchUdp() 在 executor 模式下会复制；直接回调模式
            // 要求上层当场消费，不得保存指针。
            net_io_->DispatchUdp(callbacks_, id_, FromSockAddr(peer), buffer,
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
