#ifndef LIVE_STREAM_NET_SRC_UDP_SOCKET_H_
#define LIVE_STREAM_NET_SRC_UDP_SOCKET_H_

#include "event_loop.h"
#include "fd.h"
#include "net.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace live_stream {
namespace net_internal {

class NetEngineImpl;

class UdpSocket : public std::enable_shared_from_this<UdpSocket> {
public:
    UdpSocket(NetEngineImpl *engine, UdpSocketId id,
                const UdpBindOptions &options, const UdpCallbacks &callbacks);
    ~UdpSocket();

    bool Start(const std::shared_ptr<EventLoop> &loop);
    void Stop();
    bool SendTo(NetAddress address, const uint8_t *data, size_t size);
    bool SendToSlices(NetAddress address, const NetBufferSlices &slices);
    bool SetPeer(NetAddress peer);
    bool SendToPeer(const uint8_t *data, size_t size);
    bool SendToPeerSlices(const NetBufferSlices &slices);
    NetAddress LocalAddress() const;
    NetAddress PeerAddress() const;

private:
    void HandleRead();
    bool SendPreparedDatagram(NetAddress address,
                              const std::shared_ptr<std::vector<uint8_t>>
                                  &datagram);
    bool SendToSlicesInLoop(NetAddress address, const NetBufferSlices &slices);

    NetEngineImpl *engine_ = nullptr;
    UdpSocketId id_ = 0;
    UdpBindOptions options_;
    UdpCallbacks callbacks_;
    std::shared_ptr<EventLoop> loop_;
    mutable std::mutex mutex_;
    UniqueFd fd_;
    NetAddress local_;
    NetAddress peer_;
    bool running_ = false;
    bool has_peer_ = false;
};

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SRC_UDP_SOCKET_H_
