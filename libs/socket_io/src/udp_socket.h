#ifndef LIVE_STREAM_SOCKET_IO_SRC_UDP_SOCKET_H_
#define LIVE_STREAM_SOCKET_IO_SRC_UDP_SOCKET_H_

#include "event_loop.h"
#include "fd.h"
#include "socket_io.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace live_stream {
namespace socket_io_internal {

class SocketIoImpl;

class UdpSocket : public std::enable_shared_from_this<UdpSocket> {
public:
    UdpSocket(SocketIoImpl *socket_io, UdpSocketId id,
              const UdpBindOptions &options, const UdpCallbacks &callbacks);
    ~UdpSocket();

    bool Start(const std::shared_ptr<EventLoop> &loop);
    void Stop();
    bool SendTo(SocketAddress address, const uint8_t *data, size_t size);
    bool SendToSlices(SocketAddress address, const SocketWriteSlices &slices);
    bool SetPeer(SocketAddress peer);
    bool SendToPeer(const uint8_t *data, size_t size);
    bool SendToPeerSlices(const SocketWriteSlices &slices);
    SocketAddress LocalAddress() const;
    SocketAddress PeerAddress() const;

private:
    void HandleRead();
    bool SendPreparedDatagram(SocketAddress address,
                              const std::shared_ptr<std::vector<uint8_t>>
                                  &datagram);
    bool SendToSlicesInLoop(SocketAddress address, const SocketWriteSlices &slices);

    SocketIoImpl *socket_io_ = nullptr;
    UdpSocketId id_ = 0;
    UdpBindOptions options_;
    UdpCallbacks callbacks_;
    std::shared_ptr<EventLoop> loop_;
    mutable std::mutex mutex_;
    UniqueFd fd_;
    SocketAddress local_;
    SocketAddress peer_;
    bool running_ = false;
    bool has_peer_ = false;
};

}  // namespace socket_io_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_SRC_UDP_SOCKET_H_
