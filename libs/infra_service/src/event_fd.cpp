#include "infra/event_fd.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

namespace infra {

Status EventFd::Open() {
    if (fd_.valid()) {
        return Status::kOk;
    }
    const int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        return Status::kIoError;
    }
    fd_.Reset(fd);
    return Status::kOk;
}

Status EventFd::Notify() {
    if (!fd_.valid()) {
        return Status::kBusy;
    }
    const uint64_t value = 1;
    const ssize_t ret = write(fd_.get(), &value, sizeof(value));
    if (ret == static_cast<ssize_t>(sizeof(value)) || errno == EAGAIN) {
        return Status::kOk;
    }
    return Status::kIoError;
}

void EventFd::Drain() {
    if (!fd_.valid()) {
        return;
    }
    uint64_t value = 0;
    while (read(fd_.get(), &value, sizeof(value)) > 0) {
    }
}

}  // namespace infra
