#ifndef LIVE_STREAM_NET_NET_H_
#define LIVE_STREAM_NET_NET_H_

#include "infra/executor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {

struct NetAddress {
    std::string ip = "0.0.0.0";
    uint16_t port = 0;
};

using ConnectionId = uint64_t;
using TcpServerId = uint64_t;
using UdpSocketId = uint64_t;
using NetTimerId = uint64_t;

constexpr size_t kMaxNetBufferSlices = 8;

using NetBufferOwnerRefFn = void (*)(const void *owner);
using NetBufferOwnerUnrefFn = void (*)(const void *owner);

struct NetBufferOwner {
    const void *ptr = nullptr;
    NetBufferOwnerRefFn ref = nullptr;
    NetBufferOwnerUnrefFn unref = nullptr;
};

struct NetBufferSlice {
    const uint8_t *data = nullptr;
    size_t size = 0;
    NetBufferOwner owner;
};

struct NetBufferSlices {
    NetBufferSlice slices[kMaxNetBufferSlices];
    size_t count = 0;

    bool Add(const uint8_t *data, size_t size,
             NetBufferOwner owner = NetBufferOwner{}) {
        if (size == 0) {
            return true;
        }
        if (data == nullptr || count >= kMaxNetBufferSlices ||
            (owner.ptr != nullptr &&
             (owner.ref == nullptr || owner.unref == nullptr))) {
            return false;
        }
        slices[count].data = data;
        slices[count].size = size;
        slices[count].owner = owner;
        ++count;
        return true;
    }

    size_t TotalSize() const {
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            total += slices[i].size;
        }
        return total;
    }
};

enum class CallbackMode {
    kInlineOnIo,
    kPostToExecutor,
};

enum class TcpCloseReason {
    kNormal = 0,
    kRemoteClose,
    kParseError,
    kAuthFailed,
    kQueueFull,
    kPendingLimit,
    kSendStall,
    kReadTimeout,
    kWriteTimeout,
    kInternalError,
};

struct NetEngineOptions {
    uint32_t io_threads = 1;
    uint32_t max_events_per_loop = 64;
    uint32_t task_queue_capacity = 4096;
    CallbackMode callback_mode = CallbackMode::kInlineOnIo;
    infra::Executor *callback_executor = nullptr;
};

struct TcpListenOptions {
    NetAddress address;
    std::string owner_protocol;
    uint32_t backlog = 128;
    uint32_t max_connections = 64;
    uint32_t send_queue_capacity = 128;
    uint32_t send_buffer_limit_bytes = 1024 * 1024;
    uint32_t send_stall_timeout_ms = 0;
    uint32_t read_timeout_ms = 0;
    uint32_t write_timeout_ms = 0;
    bool reuse_port = false;
    bool tcp_no_delay = false;
    bool keepalive = false;
};

struct UdpBindOptions {
    NetAddress address;
    uint32_t recv_buffer_bytes = 0;
    uint32_t send_buffer_bytes = 0;
};

using TcpAcceptFn = void (*)(void *user, ConnectionId id, NetAddress peer);
using TcpReadFn = void (*)(void *user, ConnectionId id, const uint8_t *data,
                           size_t size);
using TcpCloseFn = void (*)(void *user, ConnectionId id,
                            TcpCloseReason reason);
using UdpReadFn = void (*)(void *user, UdpSocketId socket_id, NetAddress peer,
                           const uint8_t *data, size_t size);

struct TcpCallbacks {
    void *user = nullptr;
    TcpAcceptFn on_accept = nullptr;
    TcpReadFn on_read = nullptr;
    TcpCloseFn on_close = nullptr;
};

struct UdpCallbacks {
    void *user = nullptr;
    UdpReadFn on_read = nullptr;
};

struct NetStats {
    uint32_t active_connections = 0;
    uint64_t accepted_connections = 0;
    uint64_t rejected_connections = 0;
    uint64_t closed_connections = 0;
    uint64_t read_bytes = 0;
    uint64_t written_bytes = 0;
    uint64_t sent_datagrams = 0;
    uint64_t received_datagrams = 0;
    uint64_t send_busy_count = 0;
    uint64_t slow_client_closes = 0;
};

struct NetConnectionDiagnostics {
    ConnectionId connection_id = 0;
    std::string owner_protocol;
    NetAddress remote_address;
    NetAddress local_address;
    uint32_t pending_bytes = 0;
    uint32_t send_queue_length = 0;
    int64_t last_write_at_ms = 0;
    TcpCloseReason close_reason = TcpCloseReason::kNormal;
    bool open = false;
};

const char *TcpCloseReasonName(TcpCloseReason reason);

class NetEngine {
public:
    virtual ~NetEngine() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;

    virtual TcpServerId ListenTcp(const TcpListenOptions &options,
                                  const TcpCallbacks &callbacks) = 0;
    virtual bool CloseTcp(TcpServerId id) = 0;
    virtual UdpSocketId BindUdp(const UdpBindOptions &options,
                                const UdpCallbacks &callbacks) = 0;
    virtual bool CloseUdp(UdpSocketId id) = 0;

    virtual bool Send(ConnectionId id, const uint8_t *data, size_t size) = 0;
    virtual bool SendSlices(ConnectionId id, const NetBufferSlices &slices) {
        for (size_t i = 0; i < slices.count; ++i) {
            if (!Send(id, slices.slices[i].data, slices.slices[i].size)) {
                return false;
            }
        }
        return true;
    }
    virtual bool Close(ConnectionId id) = 0;
    virtual bool Close(ConnectionId id, TcpCloseReason reason) {
        (void)reason;
        return Close(id);
    }
    virtual bool CloseAfterSend(ConnectionId id) = 0;
    virtual bool SendTo(UdpSocketId id, NetAddress address, const uint8_t *data,
                        size_t size) = 0;
    virtual bool SendToSlices(UdpSocketId id, NetAddress address,
                              const NetBufferSlices &slices) {
        if (slices.count != 1) {
            return false;
        }
        return SendTo(id, std::move(address), slices.slices[0].data,
                      slices.slices[0].size);
    }
    virtual bool SetUdpPeer(UdpSocketId id, NetAddress peer) = 0;
    virtual bool SendToPeer(UdpSocketId id, const uint8_t *data,
                            size_t size) = 0;
    virtual bool SendToPeerSlices(UdpSocketId id,
                                  const NetBufferSlices &slices) {
        if (slices.count != 1) {
            return false;
        }
        return SendToPeer(id, slices.slices[0].data, slices.slices[0].size);
    }

    virtual NetTimerId RunOnIoAfter(uint32_t delay_ms, infra::Task task) = 0;
    virtual NetTimerId RunOnIoEvery(uint32_t interval_ms,
                                    infra::Task task) = 0;
    virtual bool CancelIoTimer(NetTimerId id) = 0;

    virtual NetAddress TcpLocalAddress(TcpServerId id) const = 0;
    virtual NetAddress UdpLocalAddress(UdpSocketId id) const = 0;
    virtual NetAddress UdpPeerAddress(UdpSocketId id) const = 0;
    virtual uint32_t PendingBytes(ConnectionId id) const = 0;
    virtual NetConnectionDiagnostics GetConnectionDiagnostics(
        ConnectionId id) const {
        (void)id;
        return NetConnectionDiagnostics{};
    }
    virtual std::vector<NetConnectionDiagnostics>
    GetConnectionDiagnosticsSnapshot() const {
        return std::vector<NetConnectionDiagnostics>();
    }
    virtual NetStats GetStats() const = 0;
};

std::unique_ptr<NetEngine> CreateNetEngine(const NetEngineOptions &options);

}  // namespace live_stream

#endif  // LIVE_STREAM_NET_NET_H_
