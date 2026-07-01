#ifndef LIVE_STREAM_SOCKET_IO_SRC_SOCKET_IO_IMPL_H_
#define LIVE_STREAM_SOCKET_IO_SRC_SOCKET_IO_IMPL_H_

#include "event_loop.h"
#include "socket_io.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace live_stream {
namespace socket_io_internal {

class TcpSession;
class TcpServer;
class UdpSocket;

class SocketIoImpl : public ISocketIo {
public:
    explicit SocketIoImpl(const SocketIoOptions &options);
    ~SocketIoImpl() override;

    bool Start() override;
    void Stop() override;
    event::Loop *DefaultLoop() override;
    event::Loop *PickLoop() override;
    TcpServerId ListenTcp(event::Loop *loop,
                          const TcpListenOptions &options,
                          const TcpCallbacks &callbacks) override;
    bool CloseTcp(TcpServerId id) override;
    UdpSocketId BindUdp(event::Loop *loop,
                        const UdpBindOptions &options,
                        const UdpCallbacks &callbacks) override;
    bool CloseUdp(UdpSocketId id) override;
    bool Send(ConnectionId id, const uint8_t *data, size_t size) override;
    bool SendSlices(ConnectionId id, const SocketWriteSlices &slices) override;
    bool Close(ConnectionId id) override;
    bool Close(ConnectionId id, TcpCloseReason reason) override;
    bool CloseAfterSend(ConnectionId id) override;
    bool SendTo(UdpSocketId id, SocketAddress address, const uint8_t *data,
                size_t size) override;
    bool SendToSlices(UdpSocketId id, SocketAddress address,
                      const SocketWriteSlices &slices) override;
    bool SetUdpPeer(UdpSocketId id, SocketAddress peer) override;
    bool SendToPeer(UdpSocketId id, const uint8_t *data,
                    size_t size) override;
    bool SendToPeerSlices(UdpSocketId id,
                          const SocketWriteSlices &slices) override;
    SocketAddress TcpLocalAddress(TcpServerId id) const override;
    SocketAddress UdpLocalAddress(UdpSocketId id) const override;
    SocketAddress UdpPeerAddress(UdpSocketId id) const override;
    uint32_t PendingBytes(ConnectionId id) const override;
    SocketConnectionInfo GetConnectionInfo(
        ConnectionId id) const override;
    std::vector<SocketConnectionInfo>
    ListConnectionInfo() const override;
    SocketIoStats GetStats() const override;

    bool AddAcceptedConnection(
        const std::shared_ptr<TcpSession> &connection,
        uint32_t max_connections);
    void RemoveConnection(ConnectionId id);
    void OnConnectionClosed(ConnectionId id, const TcpCallbacks &callbacks,
                            TcpCloseReason reason,
                            SocketConnectionInfo info);
    void DispatchAccept(const TcpCallbacks &callbacks, ConnectionId id,
                        SocketAddress peer);
    void DispatchRead(const TcpCallbacks &callbacks, ConnectionId id,
                      const uint8_t *data, size_t size);
    void DispatchClose(const TcpCallbacks &callbacks, ConnectionId id,
                       TcpCloseReason reason);
    void DispatchUdp(const UdpCallbacks &callbacks, UdpSocketId socket_id,
                     SocketAddress peer, const uint8_t *data, size_t size);
    std::shared_ptr<EventLoop> ResolveLoop(event::Loop *loop) const;
    ConnectionId AllocateConnectionId() { return next_connection_id_++; }
    void AddAccepted();
    void AddRejected();
    void AddRead(size_t size);
    void AddWrite(size_t size);
    void AddSendBusy();
    void AddSlowClose();
    void AddUdpRx();
    void AddUdpTx();

private:
    void StopInternal();
    std::shared_ptr<TcpSession> FindConnection(ConnectionId id) const;
    void RememberClosedConnectionLocked(
        const SocketConnectionInfo &info);

    SocketIoOptions options_;
    mutable std::mutex mutex_;
    mutable std::mutex stats_mutex_;
    // executors_ 由调用方显式 pick；listener、accepted session、UDP endpoint
    // 和 timer 都绑定到指定执行域。
    std::vector<std::shared_ptr<EventLoop>> loops_;
    // servers_/udp_sockets_/connections_ 是 socket_io 的资源所有权表；协议模块只保存 id，
    // 不能保存内部对象指针。
    std::unordered_map<TcpServerId, std::shared_ptr<TcpServer>> servers_;
    std::unordered_map<UdpSocketId, std::shared_ptr<UdpSocket>> udp_sockets_;
    std::unordered_map<ConnectionId, std::shared_ptr<TcpSession>> connections_;
    // 关闭诊断只保留最近一小段历史，用于 Web 排查慢客户端和超时原因；
    // 不把它当作 session 生命周期来源。
    std::unordered_map<ConnectionId, SocketConnectionInfo>
        closed_connections_;
    std::deque<ConnectionId> closed_connection_order_;
    std::atomic<uint64_t> next_server_id_{1};
    std::atomic<uint64_t> next_udp_id_{1};
    std::atomic<uint64_t> next_connection_id_{1};
    mutable std::atomic<uint32_t> next_loop_{0};
    std::atomic<bool> running_{false};
    mutable SocketIoStats stats_;
};

}  // namespace socket_io_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_SRC_SOCKET_IO_IMPL_H_
