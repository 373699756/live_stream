#include "event_fd.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

namespace live_stream {
namespace net_internal {

bool EventFd::Open() {
    if (fd_.valid()) {
        return true;
    }
    const int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    fd_.Reset(fd);
    return true;
}

bool EventFd::Notify() {
    if (!fd_.valid()) {
        return false;
    }
    const uint64_t value = 1;
    const ssize_t ret = write(fd_.get(), &value, sizeof(value));
    if (ret == static_cast<ssize_t>(sizeof(value)) || errno == EAGAIN) {
        return true;
    }
    return false;
}

void EventFd::Drain() {
    if (!fd_.valid()) {
        return;
    }
    uint64_t value = 0;
    while (read(fd_.get(), &value, sizeof(value)) > 0) {
    }
}

}  // namespace net_internal
}  // namespace live_stream
