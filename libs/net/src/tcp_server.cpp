#include "tcp_server.h"

#include "net_engine_impl.h"
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

TcpServer::TcpServer(NetEngineImpl *engine, TcpServerId id,
                     const TcpListenOptions &options,
                     const TcpCallbacks &callbacks)
    : engine_(engine), id_(id), options_(options), callbacks_(callbacks) {}

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
        if (!engine_->CanAccept(options_.max_connections)) {
            // 超过协议配置的连接上限时直接丢弃 accepted fd；UniqueFd 析构会关闭它。
            engine_->AddRejected();
            continue;
        }
        int enabled = 1;
        if (options_.tcp_no_delay) {
            (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        }
        if (options_.keepalive) {
            (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
        }
        NetAddress local = GetSocketAddress(fd, false);
        if (local.port == 0) {
            engine_->AddRejected();
            continue;
        }
        const ConnectionId id = engine_->AllocateConnectionId();
        auto connection = std::make_shared<TcpSession>(
            engine_, engine_->NextLoop(), accepted.Release(), id, options_,
            callbacks_, local, FromSockAddr(peer_addr));
        if (!connection->Start()) {
            engine_->AddRejected();
            continue;
        }
        // Start() 先把 fd 加入 IO loop，再注册到 engine 连接表。这样 on_read/on_close
        // 回调到达时，协议层已经能通过 connection id 查询诊断。
        engine_->RegisterConnection(connection);
        engine_->AddAccepted();
        engine_->DispatchAccept(callbacks_, id, connection->peer());
    }
}

}  // namespace net_internal
}  // namespace live_stream
