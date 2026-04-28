#include "socket_util.h"

#include "infra/errno_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>

namespace live_stream {
namespace netframe_internal {

infra::Status SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return infra::ErrnoToStatus(errno);
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return infra::ErrnoToStatus(errno);
  }
  return infra::Status::kOk;
}

infra::Result<sockaddr_in> ToSockAddr(const NetAddress& address) {
  if (address.ip.empty()) {
    return infra::Result<sockaddr_in>::Fail(infra::Status::kInvalidParam);
  }
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(address.port);
  if (inet_pton(AF_INET, address.ip.c_str(), &addr.sin_addr) != 1) {
    return infra::Result<sockaddr_in>::Fail(infra::Status::kInvalidParam);
  }
  return infra::Result<sockaddr_in>::Ok(addr);
}

NetAddress FromSockAddr(const sockaddr_in& addr) {
  char ip[INET_ADDRSTRLEN] = {};
  const char* result = inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
  NetAddress address;
  address.ip = result == nullptr ? std::string() : std::string(ip);
  address.port = ntohs(addr.sin_port);
  return address;
}

infra::Result<NetAddress> GetSocketAddress(int fd, bool peer) {
  sockaddr_in addr {};
  socklen_t len = sizeof(addr);
  const int ret =
      peer ? getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len)
           : getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
  if (ret != 0) {
    return infra::Result<NetAddress>::Fail(infra::ErrnoToStatus(errno));
  }
  return infra::Result<NetAddress>::Ok(FromSockAddr(addr));
}

}  // namespace netframe_internal
}  // namespace live_stream
