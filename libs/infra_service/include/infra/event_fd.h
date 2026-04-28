#ifndef LIVE_STREAM_INFRA_EVENT_FD_H_
#define LIVE_STREAM_INFRA_EVENT_FD_H_

#include "infra/fd.h"
#include "infra/status.h"

namespace infra {

class EventFd {
 public:
    EventFd() = default;
    ~EventFd() = default;

    EventFd(const EventFd&) = delete;
    EventFd& operator=(const EventFd&) = delete;

    Status Open();
    int fd() const { return fd_.get(); }
    bool valid() const { return fd_.valid(); }
    Status Notify();
    void Drain();
    void Close() { fd_.Reset(); }

 private:
    UniqueFd fd_;
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_EVENT_FD_H_
