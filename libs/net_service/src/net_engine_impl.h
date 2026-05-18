#ifndef LIVE_STREAM_NET_SERVICE_SRC_NET_ENGINE_IMPL_H_
#define LIVE_STREAM_NET_SERVICE_SRC_NET_ENGINE_IMPL_H_

#include "event_loop.h"
#include "net_service.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace live_stream {
namespace net_internal {

class TcpConnection;
class TcpServer;
class UdpEndpoint;

class NetEngineImpl : public NetEngine {
public:
    explicit NetEngineImpl(const NetEngineOptions &options);
    ~NetEngineImpl() override;

    bool Start() override;
    void Stop() override;
    TcpServerId ListenTcp(const TcpListenOptions &options,
                          const TcpCallbacks &callbacks) override;
    bool CloseTcp(TcpServerId id) override;
    UdpSocketId BindUdp(const UdpBindOptions &options,
                        const UdpCallbacks &callbacks) override;
    bool CloseUdp(UdpSocketId id) override;
    bool Send(ConnectionId id, const uint8_t *data, size_t size) override;
    bool SendSlices(ConnectionId id, const NetBufferSlices &slices) override;
    bool Close(ConnectionId id) override;
    bool CloseAfterSend(ConnectionId id) override;
    bool SendTo(UdpSocketId id, NetAddress address, const uint8_t *data,
                size_t size) override;
    bool SendToSlices(UdpSocketId id, NetAddress address,
                      const NetBufferSlices &slices) override;
    NetTimerId RunOnIoAfter(uint32_t delay_ms, infra::Task task) override;
    bool CancelIoTimer(NetTimerId id) override;
    NetAddress TcpLocalAddress(TcpServerId id) const override;
    NetAddress UdpLocalAddress(UdpSocketId id) const override;
    uint32_t PendingBytes(ConnectionId id) const override;
    NetStats GetStats() const override;

    void RegisterConnection(const std::shared_ptr<TcpConnection> &connection);
    void OnConnectionClosed(ConnectionId id, const TcpCallbacks &callbacks);
    void DispatchAccept(const TcpCallbacks &callbacks, ConnectionId id,
                        NetAddress peer);
    void DispatchRead(const TcpCallbacks &callbacks, ConnectionId id,
                      const uint8_t *data, size_t size);
    void DispatchClose(const TcpCallbacks &callbacks, ConnectionId id);
    void DispatchUdp(const UdpCallbacks &callbacks, UdpSocketId socket_id,
                     NetAddress peer, const uint8_t *data, size_t size);
    std::shared_ptr<EventLoop> NextLoop();
    ConnectionId AllocateConnectionId() { return next_connection_id_++; }
    void AddAccepted();
    void AddRejected();
    void AddRead(size_t size);
    void AddWrite(size_t size);
    void AddSendBusy();
    void AddSlowClose();
    void AddUdpRx();
    void AddUdpTx();
    bool CanAccept(uint32_t max_connections) const;

private:
    std::shared_ptr<TcpConnection> FindConnection(ConnectionId id) const;

    NetEngineOptions options_;
    mutable std::mutex mutex_;
    mutable std::mutex stats_mutex_;
    std::vector<std::shared_ptr<EventLoop>> loops_;
    std::unordered_map<TcpServerId, std::shared_ptr<TcpServer>> servers_;
    std::unordered_map<UdpSocketId, std::shared_ptr<UdpEndpoint>> udp_sockets_;
    std::unordered_map<ConnectionId, std::shared_ptr<TcpConnection>> connections_;
    std::atomic<uint64_t> next_server_id_{1};
    std::atomic<uint64_t> next_udp_id_{1};
    std::atomic<uint64_t> next_connection_id_{1};
    std::atomic<uint32_t> next_loop_{0};
    std::atomic<bool> running_{false};
    mutable NetStats stats_;
};

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SERVICE_SRC_NET_ENGINE_IMPL_H_
