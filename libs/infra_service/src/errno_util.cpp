#include "infra/errno_util.h"

#include <cerrno>

namespace infra {

Status ErrnoToStatus(int error) {
    if (error == EINVAL || error == EADDRNOTAVAIL || error == EAFNOSUPPORT) {
        return Status::kInvalidParam;
    }
    if (error == EADDRINUSE) {
        return Status::kAlreadyExists;
    }
    if (error == EACCES || error == EPERM) {
        return Status::kNoPermission;
    }
    if (error == ENOMEM || error == ENOBUFS) {
        return Status::kNoMemory;
    }
    if (error == EAGAIN || error == EWOULDBLOCK) {
        return Status::kBusy;
    }
    if (error == ETIMEDOUT) {
        return Status::kTimeout;
    }
    return Status::kIoError;
}

}  // namespace infra
