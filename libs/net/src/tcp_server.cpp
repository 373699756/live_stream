#include "tcp_server.h"

#include "net_io_impl.h"
#include "socket_util.h"
#include "tcp_session.h"

#include "infra/log.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <cerrno>
#include <utility>

namespace live_stream {
namespace net_internal {
namespace {

constexpr const char *kModuleName = "net";

}  // namespace

TcpServer::TcpServer(NetIoImpl *net_io, TcpServerId id,
                     const TcpListenOptions &options,
                     const TcpCallbacks &callbacks)
    : net_io_(net_io), id_(id), options_(options), callbacks_(callbacks) {}

TcpServer::~TcpServer() { Stop(); }

bool TcpServer::Start(const std::shared_ptr<EventLoop> &loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    if (!loop || options_.backlog == 0 || options_.max_connections == 0 ||
        options_.send_queue_capacity == 0 ||
        options_.send_buffer_limit_bytes == 0) {
        Error(kModuleName,
              "TCP listen invalid options ip=%s port=%u loop=%d "
              "backlog=%u max_conn=%u send_q=%u send_limit=%u",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port),
              loop ? 1 : 0,
              static_cast<unsigned>(options_.backlog),
              static_cast<unsigned>(options_.max_connections),
              static_cast<unsigned>(options_.send_queue_capacity),
              static_cast<unsigned>(
                  options_.send_buffer_limit_bytes));
        return false;
    }
    sockaddr_in addr = ToSockAddr(options_.address);
    if (addr.sin_family != AF_INET) {
        Error(kModuleName, "TCP listen invalid address ip=%s port=%u",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port));
        return false;
    }
    UniqueFd fd(CreateSocket(AF_INET, SOCK_STREAM, 0));
    if (!fd.valid()) {
        const int error = errno;
        Error(kModuleName,
              "TCP socket failed ip=%s port=%u errno=%d (%s)",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port), error,
              ErrnoText(error));
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
        const int error = errno;
        Error(kModuleName,
              "TCP nonblock failed ip=%s port=%u errno=%d (%s)",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port), error,
              ErrnoText(error));
        return false;
    }
    if (bind(fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) !=
        0) {
        const int error = errno;
        Error(kModuleName,
              "TCP bind failed ip=%s port=%u errno=%d (%s)",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port), error,
              ErrnoText(error));
        return false;
    }
    if (listen(fd.get(), static_cast<int>(options_.backlog)) != 0) {
        const int error = errno;
        Error(kModuleName,
              "TCP listen failed ip=%s port=%u errno=%d (%s)",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port), error,
              ErrnoText(error));
        return false;
    }
    NetAddress local = GetSocketAddress(fd.get(), false);
    if (local.port == 0) {
        Error(kModuleName,
              "TCP local address unavailable ip=%s port=%u",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port));
        return false;
    }
    loop_ = loop;
    listen_fd_ = std::move(fd);
    local_ = local;
    running_ = true;
    std::weak_ptr<TcpServer> weak_self = shared_from_this();
    if (!loop_->AddFd(listen_fd_.get(), EPOLLIN, [weak_self](uint32_t events) {
            auto self = weak_self.lock();
            if (self && (events & EPOLLIN) != 0) {
                self->AcceptLoop();
            }
        })) {
        Error(kModuleName,
              "TCP epoll add failed ip=%s port=%u local=%s:%u",
              options_.address.ip.c_str(),
              static_cast<unsigned>(options_.address.port),
              local.ip.c_str(), static_cast<unsigned>(local.port));
        running_ = false;
        listen_fd_.Reset();
        loop_.reset();
        return false;
    }
    Info(kModuleName, "TCP listening ip=%s port=%u local=%s:%u",
         options_.address.ip.c_str(),
         static_cast<unsigned>(options_.address.port),
         local.ip.c_str(), static_cast<unsigned>(local.port));
    return true;
}

void TcpServer::Stop() {
    std::shared_ptr<EventLoop> loop;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        loop = loop_;
        fd = listen_fd_.get();
    }
    if (loop && fd >= 0) {
        loop->RemoveFd(fd);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    listen_fd_.Reset();
    loop_.reset();
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
        sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);
        const int fd = AcceptSocket(listen_fd,
                                    reinterpret_cast<sockaddr *>(&peer_addr),
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
            net_io_->AddRejected();
            continue;
        }
        std::shared_ptr<EventLoop> session_loop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session_loop = loop_;
        }
        if (!session_loop) {
            net_io_->AddRejected();
            continue;
        }
        const ConnectionId id = net_io_->AllocateConnectionId();
        auto connection = std::make_shared<TcpSession>(
            net_io_, session_loop, accepted.Release(), id, options_,
            callbacks_, local, FromSockAddr(peer_addr));
        if (!net_io_->AddAcceptedConnection(connection,
                                            options_.max_connections)) {
            net_io_->AddRejected();
            continue;
        }
        if (!connection->Start()) {
            net_io_->RemoveConnection(id);
            net_io_->AddRejected();
            continue;
        }
        // fd 加入 IO loop 前先注册到连接表，后续 read/close 回调到达时，
        // 协议层已经能通过 connection id 查询诊断。
        net_io_->AddAccepted();
        net_io_->DispatchAccept(callbacks_, id, connection->peer());
    }
}

}  // namespace net_internal
}  // namespace live_stream
