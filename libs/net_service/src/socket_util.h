#ifndef LIVE_STREAM_NET_SERVICE_SRC_SOCKET_UTIL_H_
#define LIVE_STREAM_NET_SERVICE_SRC_SOCKET_UTIL_H_

#include "net_service.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

namespace live_stream {
namespace net_internal {

const char *ErrnoText(int error);
bool SetCloseOnExec(int fd);
bool SetNonBlocking(int fd);
int CreateSocket(int domain, int type, int protocol);
int AcceptSocket(int fd, sockaddr *addr, socklen_t *addr_len, int flags);
int CreateEventFd(unsigned int initval, int flags);
int CreateEpollFd(int flags);
int CreateTimerFd(clockid_t clockid, int flags);
sockaddr_in ToSockAddr(const NetAddress &address);
NetAddress FromSockAddr(const sockaddr_in &addr);
NetAddress GetSocketAddress(int fd, bool peer);

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SERVICE_SRC_SOCKET_UTIL_H_
