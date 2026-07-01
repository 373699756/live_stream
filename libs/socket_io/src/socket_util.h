#ifndef LIVE_STREAM_SOCKET_IO_SRC_SOCKET_UTIL_H_
#define LIVE_STREAM_SOCKET_IO_SRC_SOCKET_UTIL_H_

#include "socket_io.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

namespace live_stream {
namespace socket_io_internal {

const char *ErrnoText(int error);
bool SetCloseOnExec(int fd);
bool SetNonBlocking(int fd);
int CreateSocket(int domain, int type, int protocol);
int AcceptSocket(int fd, sockaddr *addr, socklen_t *addr_len, int flags);
int CreateEventFd(unsigned int initval, int flags);
int CreateEpollFd(int flags);
int CreateTimerFd(clockid_t clockid, int flags);
sockaddr_in ToSockAddr(const SocketAddress &address);
SocketAddress FromSockAddr(const sockaddr_in &addr);
SocketAddress GetSocketAddress(int fd, bool peer);

}  // namespace socket_io_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_SRC_SOCKET_UTIL_H_
