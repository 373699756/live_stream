#include "fd.h"

#include <unistd.h>

namespace live_stream {
namespace net_internal {

UniqueFd::~UniqueFd() { Reset(); }

UniqueFd::UniqueFd(UniqueFd &&other) noexcept : fd_(other.Release()) {}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
  if (this != &other) {
    Reset(other.Release());
  }
  return *this;
}

int UniqueFd::Release() {
  const int fd = fd_;
  fd_ = -1;
  return fd;
}

void UniqueFd::Reset(int fd) {
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = fd;
}

} // namespace net_internal
} // namespace live_stream
