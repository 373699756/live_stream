#ifndef LIVE_STREAM_SOCKET_IO_SRC_EVENT_FD_H_
#define LIVE_STREAM_SOCKET_IO_SRC_EVENT_FD_H_

#include "fd.h"

namespace live_stream {
namespace socket_io_internal {

class EventFd {
public:
    EventFd() = default;
    ~EventFd() = default;

    EventFd(const EventFd &) = delete;
    EventFd &operator=(const EventFd &) = delete;

    bool Open();
    int fd() const { return fd_.get(); }
    bool valid() const { return fd_.valid(); }
    bool Notify();
    void Drain();
    void Close() { fd_.Reset(); }

private:
    UniqueFd fd_;
};

}  // namespace socket_io_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_SRC_EVENT_FD_H_
