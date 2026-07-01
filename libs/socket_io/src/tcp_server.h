#ifndef LIVE_STREAM_SOCKET_IO_SRC_TCP_SERVER_H_
#define LIVE_STREAM_SOCKET_IO_SRC_TCP_SERVER_H_

#include "event_loop.h"
#include "fd.h"
#include "socket_io.h"

#include <memory>
#include <mutex>

namespace live_stream {
namespace socket_io_internal {

class SocketIoImpl;

class TcpServer : public std::enable_shared_from_this<TcpServer> {
public:
    TcpServer(SocketIoImpl *socket_io, TcpServerId id,
              const TcpListenOptions &options, const TcpCallbacks &callbacks);
    ~TcpServer();

    bool Start(const std::shared_ptr<EventLoop> &loop);
    void Stop();
    SocketAddress LocalAddress() const;

private:
    void AcceptLoop();

    SocketIoImpl *socket_io_ = nullptr;
    TcpServerId id_ = 0;
    TcpListenOptions options_;
    TcpCallbacks callbacks_;
    std::shared_ptr<EventLoop> loop_;
    mutable std::mutex mutex_;
    UniqueFd listen_fd_;
    SocketAddress local_;
    bool running_ = false;
};

}  // namespace socket_io_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_SRC_TCP_SERVER_H_
