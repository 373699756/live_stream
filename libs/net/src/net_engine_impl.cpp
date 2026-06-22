#include "net_engine_impl.h"

#include "tcp_session.h"
#include "tcp_server.h"
#include "udp_socket.h"

#include <utility>
#include <vector>

namespace live_stream {
namespace net_internal {
namespace {

constexpr size_t kClosedConnectionInfoLimit = 128;

}  // namespace

NetEngineImpl::NetEngineImpl(const NetEngineOptions &options)
    : options_(options) {}

NetEngineImpl::~NetEngineImpl() { StopInternal(); }

bool NetEngineImpl::Start() {
    if (options_.io_threads == 0) {
        return false;
    }
    if (options_.callback_mode == CallbackMode::kPostToLoop &&
        options_.callback_loop == nullptr) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return true;
        }
        if (loops_.empty()) {
            for (uint32_t i = 0; i < options_.io_threads; ++i) {
                const int affinity_cpu =
                    options_.enable_thread_affinity
                        ? static_cast<int>(options_.first_io_cpu + i)
                        : -1;
                auto loop = std::make_shared<EventLoop>(
                    options_.max_events_per_loop, options_.task_queue_capacity,
                    affinity_cpu);
                loops_.push_back(loop);
            }
        }
    }
    for (const auto &loop : loops_) {
        if (!loop->Start(event::LoopOptions())) {
            Stop();
            return false;
        }
    }
    running_ = true;
    return true;
}

void NetEngineImpl::Stop() {
    StopInternal();
}

void NetEngineImpl::StopInternal() {
    std::vector<std::shared_ptr<TcpServer>> servers;
    std::vector<std::shared_ptr<UdpSocket>> udp_sockets;
    std::vector<std::shared_ptr<TcpSession>> connections;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        for (auto &entry : servers_) {
            servers.push_back(entry.second);
        }
        for (auto &entry : udp_sockets_) {
            udp_sockets.push_back(entry.second);
        }
        for (auto &entry : connections_) {
            connections.push_back(entry.second);
        }
        servers_.clear();
        udp_sockets_.clear();
    }
    // 先停 listen/UDP endpoint，再关闭已接入 TCP session，最后停 IO loop。
    // 这样上层 close 回调仍能在 loop 停止前释放协议资源。
    for (const auto &server : servers) {
        server->Stop();
    }
    for (const auto &socket : udp_sockets) {
        socket->Stop();
    }
    for (const auto &connection : connections) {
        (void)connection->Close(TcpCloseReason::kInternalError);
    }
    for (const auto &loop : loops_) {
        loop->Stop(event::StopMode::kDiscard);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear();
}

event::Loop *NetEngineImpl::DefaultLoop() {
    std::lock_guard<std::mutex> lock(mutex_);
    return loops_.empty() ? nullptr : loops_.front().get();
}

event::Loop *NetEngineImpl::PickLoop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loops_.empty()) {
        return nullptr;
    }
    const uint32_t index =
        next_loop_.fetch_add(1) % static_cast<uint32_t>(loops_.size());
    return loops_[index].get();
}

TcpServerId NetEngineImpl::ListenTcp(event::Loop *loop,
                                     const TcpListenOptions &options,
                                     const TcpCallbacks &callbacks) {
    if (callbacks.on_read == nullptr && callbacks.on_accept == nullptr) {
        return 0;
    }
    std::shared_ptr<EventLoop> owner_loop = ResolveLoop(loop);
    if (!owner_loop) {
        return 0;
    }
    const TcpServerId id = next_server_id_++;
    auto server = std::make_shared<TcpServer>(this, id, options, callbacks);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return 0;
        }
        servers_[id] = server;
        if (!server->Start(owner_loop)) {
            servers_.erase(id);
            return 0;
        }
    }
    return id;
}

UdpSocketId NetEngineImpl::BindUdp(event::Loop *loop,
                                   const UdpBindOptions &options,
                                   const UdpCallbacks &callbacks) {
    std::shared_ptr<EventLoop> owner_loop = ResolveLoop(loop);
    if (!owner_loop) {
        return 0;
    }
    const UdpSocketId id = next_udp_id_++;
    auto socket = std::make_shared<UdpSocket>(this, id, options, callbacks);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return 0;
        }
        udp_sockets_[id] = socket;
        if (!socket->Start(owner_loop)) {
            udp_sockets_.erase(id);
            return 0;
        }
    }
    return id;
}

bool NetEngineImpl::CloseTcp(TcpServerId id) {
    std::shared_ptr<TcpServer> server;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = servers_.find(id);
        if (it == servers_.end()) {
            return false;
        }
        server = it->second;
        servers_.erase(it);
    }
    server->Stop();
    return true;
}

bool NetEngineImpl::CloseUdp(UdpSocketId id) {
    std::shared_ptr<UdpSocket> socket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = udp_sockets_.find(id);
        if (it == udp_sockets_.end()) {
            return false;
        }
        socket = it->second;
        udp_sockets_.erase(it);
    }
    socket->Stop();
    return true;
}

bool NetEngineImpl::Send(ConnectionId id, const uint8_t *data, size_t size) {
    auto connection = FindConnection(id);
    return connection ? connection->Send(data, size) : false;
}

bool NetEngineImpl::SendSlices(ConnectionId id,
                               const NetBufferSlices &slices) {
    auto connection = FindConnection(id);
    return connection ? connection->SendSlices(slices) : false;
}

bool NetEngineImpl::Close(ConnectionId id) {
    auto connection = FindConnection(id);
    return connection ? connection->Close(TcpCloseReason::kNormal) : false;
}

bool NetEngineImpl::Close(ConnectionId id, TcpCloseReason reason) {
    auto connection = FindConnection(id);
    return connection ? connection->Close(reason) : false;
}

bool NetEngineImpl::CloseAfterSend(ConnectionId id) {
    auto connection = FindConnection(id);
    return connection ? connection->CloseAfterSend() : false;
}

bool NetEngineImpl::SendTo(UdpSocketId id, NetAddress address,
                           const uint8_t *data, size_t size) {
    std::shared_ptr<UdpSocket> socket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = udp_sockets_.find(id);
        if (it == udp_sockets_.end()) {
            return false;
        }
        socket = it->second;
    }
    return socket->SendTo(std::move(address), data, size);
}

bool NetEngineImpl::SendToSlices(UdpSocketId id, NetAddress address,
                                 const NetBufferSlices &slices) {
    std::shared_ptr<UdpSocket> socket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = udp_sockets_.find(id);
        if (it == udp_sockets_.end()) {
            return false;
        }
        socket = it->second;
    }
    return socket->SendToSlices(std::move(address), slices);
}

bool NetEngineImpl::SetUdpPeer(UdpSocketId id, NetAddress peer) {
    std::shared_ptr<UdpSocket> socket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = udp_sockets_.find(id);
        if (it == udp_sockets_.end()) {
            return false;
        }
        socket = it->second;
    }
    return socket->SetPeer(std::move(peer));
}

bool NetEngineImpl::SendToPeer(UdpSocketId id, const uint8_t *data,
                               size_t size) {
    std::shared_ptr<UdpSocket> socket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = udp_sockets_.find(id);
        if (it == udp_sockets_.end()) {
            return false;
        }
        socket = it->second;
    }
    return socket->SendToPeer(data, size);
}

bool NetEngineImpl::SendToPeerSlices(UdpSocketId id,
                                     const NetBufferSlices &slices) {
    std::shared_ptr<UdpSocket> socket;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = udp_sockets_.find(id);
        if (it == udp_sockets_.end()) {
            return false;
        }
        socket = it->second;
    }
    return socket->SendToPeerSlices(slices);
}

NetAddress NetEngineImpl::TcpLocalAddress(TcpServerId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = servers_.find(id);
    if (it == servers_.end()) {
        return NetAddress{};
    }
    return it->second->LocalAddress();
}

NetAddress NetEngineImpl::UdpLocalAddress(UdpSocketId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = udp_sockets_.find(id);
    if (it == udp_sockets_.end()) {
        return NetAddress{};
    }
    return it->second->LocalAddress();
}

NetAddress NetEngineImpl::UdpPeerAddress(UdpSocketId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = udp_sockets_.find(id);
    if (it == udp_sockets_.end()) {
        return NetAddress{};
    }
    return it->second->PeerAddress();
}

uint32_t NetEngineImpl::PendingBytes(ConnectionId id) const {
    auto connection = FindConnection(id);
    return connection ? connection->PendingBytes() : 0;
}

NetConnectionInfo NetEngineImpl::GetConnectionInfo(
    ConnectionId id) const {
    auto connection = FindConnection(id);
    if (connection) {
        return connection->GetInfo();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = closed_connections_.find(id);
    return it == closed_connections_.end() ? NetConnectionInfo{}
                                           : it->second;
}

std::vector<NetConnectionInfo>
NetEngineImpl::ListConnectionInfo() const {
    std::vector<std::shared_ptr<TcpSession>> connections;
    std::vector<NetConnectionInfo> closed_info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections.reserve(connections_.size());
        for (const auto &entry : connections_) {
            connections.push_back(entry.second);
        }
        closed_info.reserve(closed_connection_order_.size());
        for (ConnectionId id : closed_connection_order_) {
            const auto it = closed_connections_.find(id);
            if (it != closed_connections_.end()) {
                closed_info.push_back(it->second);
            }
        }
    }

    // 活跃连接实时读取 session，已关闭连接只保留最近 N 条快照，供 Web 定位
    // 慢客户端/超时关闭原因，不让协议模块各自维护 socket info 表。
    std::vector<NetConnectionInfo> info;
    info.reserve(connections.size() + closed_info.size());
    for (const auto &connection : connections) {
        if (connection) {
            info.push_back(connection->GetInfo());
        }
    }
    info.insert(info.end(), closed_info.begin(), closed_info.end());
    return info;
}

NetStats NetEngineImpl::GetStats() const {
    NetStats stats;
    uint32_t active_connections = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_connections = static_cast<uint32_t>(connections_.size());
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats = stats_;
    }
    stats.active_connections = active_connections;
    return stats;
}

void NetEngineImpl::AddAccepted() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.accepted_connections;
}

void NetEngineImpl::AddRejected() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.rejected_connections;
}

void NetEngineImpl::AddRead(size_t size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.read_bytes += size;
}

void NetEngineImpl::AddWrite(size_t size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.written_bytes += size;
}

void NetEngineImpl::AddSendBusy() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.send_busy_count;
}

void NetEngineImpl::AddSlowClose() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.slow_client_closes;
}

void NetEngineImpl::AddUdpRx() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.received_datagrams;
}

void NetEngineImpl::AddUdpTx() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++stats_.sent_datagrams;
}

bool NetEngineImpl::AddAcceptedConnection(
    const std::shared_ptr<TcpSession> &connection,
    uint32_t max_connections) {
    if (connection == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || connections_.size() >= max_connections) {
        return false;
    }
    connections_[connection->id()] = connection;
    return true;
}

void NetEngineImpl::RemoveConnection(ConnectionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(id);
}

void NetEngineImpl::OnConnectionClosed(ConnectionId id,
                                       const TcpCallbacks &callbacks,
                                       TcpCloseReason reason,
                                       NetConnectionInfo info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connections_.find(id) != connections_.end()) {
            RememberClosedConnectionLocked(info);
        }
        connections_.erase(id);
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.closed_connections;
    }
    DispatchClose(callbacks, id, reason);
}

void NetEngineImpl::RememberClosedConnectionLocked(
    const NetConnectionInfo &info) {
    if (info.connection_id == 0) {
        return;
    }
    if (closed_connections_.find(info.connection_id) ==
        closed_connections_.end()) {
        closed_connection_order_.push_back(info.connection_id);
    }
    closed_connections_[info.connection_id] = info;
    while (closed_connection_order_.size() > kClosedConnectionInfoLimit) {
        const ConnectionId oldest = closed_connection_order_.front();
        closed_connection_order_.pop_front();
        closed_connections_.erase(oldest);
    }
}

void NetEngineImpl::DispatchAccept(const TcpCallbacks &callbacks,
                                   ConnectionId id, NetAddress peer) {
    if (callbacks.on_accept == nullptr) {
        return;
    }
    if (options_.callback_mode == CallbackMode::kPostToLoop) {
        (void)options_.callback_loop->Post(
            [callbacks, id, peer = std::move(peer)]() mutable {
                callbacks.on_accept(callbacks.user, id, peer);
            });
        return;
    }
    callbacks.on_accept(callbacks.user, id, std::move(peer));
}

void NetEngineImpl::DispatchRead(const TcpCallbacks &callbacks, ConnectionId id,
                                 const uint8_t *data, size_t size) {
    if (callbacks.on_read == nullptr) {
        return;
    }
    if (options_.callback_mode == CallbackMode::kPostToLoop) {
        // data 指向 IO 栈缓冲区，投递到 callback loop 前必须复制；直接回调模式只允许
        // 协议在当前调用栈内消费，不得保存该指针。
        std::vector<uint8_t> copy(data, data + size);
        (void)options_.callback_loop->Post(
            [callbacks, id, copy = std::move(copy)]() mutable {
                callbacks.on_read(callbacks.user, id, copy.data(), copy.size());
            });
        return;
    }
    callbacks.on_read(callbacks.user, id, data, size);
}

void NetEngineImpl::DispatchClose(const TcpCallbacks &callbacks,
                                  ConnectionId id,
                                  TcpCloseReason reason) {
    if (callbacks.on_close == nullptr) {
        return;
    }
    if (options_.callback_mode == CallbackMode::kPostToLoop) {
        (void)options_.callback_loop->Post(
            [callbacks, id, reason]() {
                callbacks.on_close(callbacks.user, id, reason);
            });
        return;
    }
    callbacks.on_close(callbacks.user, id, reason);
}

void NetEngineImpl::DispatchUdp(const UdpCallbacks &callbacks,
                                UdpSocketId socket_id, NetAddress peer,
                                const uint8_t *data, size_t size) {
    if (callbacks.on_read == nullptr) {
        return;
    }
    if (options_.callback_mode == CallbackMode::kPostToLoop) {
        // UDP 接收同样来自 IO 栈缓冲区；ONVIF discovery/RTSP RTCP 等上层
        // 在 callback loop 中处理时只能看拷贝。
        std::vector<uint8_t> copy(data, data + size);
        (void)options_.callback_loop->Post([callbacks, socket_id,
                                             peer = std::move(peer),
                                             copy = std::move(copy)]() mutable {
            callbacks.on_read(callbacks.user, socket_id, peer, copy.data(),
                              copy.size());
        });
        return;
    }
    callbacks.on_read(callbacks.user, socket_id, std::move(peer), data, size);
}

std::shared_ptr<EventLoop> NetEngineImpl::ResolveLoop(
    event::Loop *loop) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loops_.empty()) {
        return nullptr;
    }
    if (loop == nullptr) {
        return nullptr;
    }
    for (const auto &candidate : loops_) {
        if (candidate.get() == loop) {
            return candidate;
        }
    }
    return nullptr;
}

std::shared_ptr<TcpSession>
NetEngineImpl::FindConnection(ConnectionId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = connections_.find(id);
    return it == connections_.end() ? nullptr : it->second;
}

}  // namespace net_internal
}  // namespace live_stream
