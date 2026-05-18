#ifndef LIVE_STREAM_NET_SERVICE_SRC_SOCKET_UTIL_H_
#define LIVE_STREAM_NET_SERVICE_SRC_SOCKET_UTIL_H_

#include "net_service.h"

#include <netinet/in.h>

namespace live_stream {
namespace net_internal {

bool SetNonBlocking(int fd);
sockaddr_in ToSockAddr(const NetAddress &address);
NetAddress FromSockAddr(const sockaddr_in &addr);
NetAddress GetSocketAddress(int fd, bool peer);

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SERVICE_SRC_SOCKET_UTIL_H_
