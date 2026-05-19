#include "socket_util.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cstring>

namespace live_stream {
namespace net_internal {

const char *ErrnoText(int error) {
    return std::strerror(error);
}

bool SetCloseOnExec(int fd) {
    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return false;
    }
    return true;
}

bool SetNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }
    return true;
}

bool SetFdFlags(int fd, bool nonblock, bool close_on_exec) {
    if (nonblock && !SetNonBlocking(fd)) {
        return false;
    }
    if (close_on_exec && !SetCloseOnExec(fd)) {
        return false;
    }
    return true;
}

int CloseFdAndReturnError(int fd) {
    const int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return -1;
}

int CreateSocket(int domain, int type, int protocol) {
    const int fd = socket(domain, type | SOCK_CLOEXEC, protocol);
    if (fd >= 0 || (errno != EINVAL && errno != ENOSYS)) {
        return fd;
    }
    const int fallback_fd = socket(domain, type, protocol);
    if (fallback_fd < 0) {
        return fallback_fd;
    }
    if (!SetFdFlags(fallback_fd, false, true)) {
        return CloseFdAndReturnError(fallback_fd);
    }
    return fallback_fd;
}

int AcceptSocket(int fd, sockaddr *addr, socklen_t *addr_len, int flags) {
    const int accepted = accept4(fd, addr, addr_len, flags);
    if (accepted >= 0 || (errno != EINVAL && errno != ENOSYS)) {
        return accepted;
    }
    const int fallback_fd = accept(fd, addr, addr_len);
    if (fallback_fd < 0) {
        return fallback_fd;
    }
    if (!SetFdFlags(fallback_fd,
                    (flags & SOCK_NONBLOCK) != 0,
                    (flags & SOCK_CLOEXEC) != 0)) {
        return CloseFdAndReturnError(fallback_fd);
    }
    return fallback_fd;
}

int CreateEventFd(unsigned int initval, int flags) {
    int fd = eventfd(initval, flags);
    if (fd >= 0 || (errno != EINVAL && errno != ENOSYS)) {
        return fd;
    }
    fd = eventfd(initval, 0);
    if (fd < 0) {
        return fd;
    }
    if (!SetFdFlags(fd,
                    (flags & EFD_NONBLOCK) != 0,
                    (flags & EFD_CLOEXEC) != 0)) {
        return CloseFdAndReturnError(fd);
    }
    return fd;
}

int CreateEpollFd(int flags) {
    int fd = epoll_create1(flags);
    if (fd >= 0 || (errno != EINVAL && errno != ENOSYS)) {
        return fd;
    }
    fd = epoll_create(1);
    if (fd < 0) {
        return fd;
    }
    if (!SetFdFlags(fd, false, (flags & EPOLL_CLOEXEC) != 0)) {
        return CloseFdAndReturnError(fd);
    }
    return fd;
}

int CreateTimerFd(clockid_t clockid, int flags) {
    int fd = timerfd_create(clockid, flags);
    if (fd >= 0 || (errno != EINVAL && errno != ENOSYS)) {
        return fd;
    }
    fd = timerfd_create(clockid, 0);
    if (fd < 0) {
        return fd;
    }
    if (!SetFdFlags(fd,
                    (flags & TFD_NONBLOCK) != 0,
                    (flags & TFD_CLOEXEC) != 0)) {
        return CloseFdAndReturnError(fd);
    }
    return fd;
}

sockaddr_in ToSockAddr(const NetAddress &address) {
    sockaddr_in addr{};
    const std::string ip = address.ip.empty() ? "0.0.0.0" : address.ip;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        std::memset(&addr, 0, sizeof(addr));
    }
    return addr;
}

NetAddress FromSockAddr(const sockaddr_in &addr) {
    char ip[INET_ADDRSTRLEN] = {};
    const char *result = inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    NetAddress address;
    address.ip = result == nullptr ? std::string() : std::string(ip);
    address.port = ntohs(addr.sin_port);
    return address;
}

NetAddress GetSocketAddress(int fd, bool peer) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    const int ret =
        peer ? getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &len)
             : getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len);
    if (ret != 0) {
        return NetAddress{};
    }
    return FromSockAddr(addr);
}

}  // namespace net_internal
}  // namespace live_stream
