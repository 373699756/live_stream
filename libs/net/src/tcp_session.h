#ifndef LIVE_STREAM_NET_SRC_TCP_SESSION_H_
#define LIVE_STREAM_NET_SRC_TCP_SESSION_H_

#include "event_loop.h"
#include "fd.h"
#include "net.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace live_stream {
namespace net_internal {

class NetEngineImpl;

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    TcpSession(NetEngineImpl *engine, std::shared_ptr<EventLoop> loop, int fd,
               ConnectionId id, const TcpListenOptions &options,
               TcpCallbacks callbacks, NetAddress local, NetAddress peer);
    ~TcpSession();

    bool Start();
    bool Send(const uint8_t *data, size_t size);
    bool SendSlices(const NetBufferSlices &slices);
    bool Close(TcpCloseReason reason);
    bool CloseAfterSend();
    uint32_t PendingBytes() const;
    ConnectionId id() const { return id_; }
    NetAddress peer() const { return peer_; }

private:
    static constexpr size_t kInlineSliceBytes = 64;

    struct OutSlice {
        OutSlice() = default;
        OutSlice(OutSlice&& other) noexcept;
        OutSlice& operator=(OutSlice&& other) noexcept;
        OutSlice(const OutSlice&) = delete;
        OutSlice& operator=(const OutSlice&) = delete;
        ~OutSlice();

        const uint8_t *data = nullptr;
        size_t size = 0;
        size_t offset = 0;
        NetBufferOwner owner;
        std::array<uint8_t, kInlineSliceBytes> inline_data{};
        std::unique_ptr<uint8_t[]> heap_data;
    };

    struct OutBuffer {
        std::array<OutSlice, kMaxNetBufferSlices> slices{};
        size_t slice_count = 0;
        size_t current_slice = 0;
        uint32_t size = 0;
        int64_t enqueue_ms = 0;
    };

    bool BuildOutBuffer(const NetBufferSlices &slices, OutBuffer *buffer) const;
    void HandleEvents(uint32_t events);
    void HandleRead();
    void HandleWrite();
    void EnableWrite();
    void DisableWrite();
    void CloseInLoop(TcpCloseReason reason);
    void ArmManagerTimer();
    void CheckTimeouts();
    bool IsReadTimedOutLocked(int64_t now_ms) const;
    bool IsWriteTimedOutLocked(int64_t now_ms) const;
    bool IsSendStalledLocked() const;
    uint32_t ManagerTickMs() const;

    NetEngineImpl *engine_ = nullptr;
    std::shared_ptr<EventLoop> loop_;
    UniqueFd fd_;
    ConnectionId id_ = 0;
    TcpListenOptions options_;
    TcpCallbacks callbacks_;
    NetAddress local_;
    NetAddress peer_;
    mutable std::mutex mutex_;
    std::deque<OutBuffer> send_queue_;
    uint32_t pending_bytes_ = 0;
    NetTimerId manager_timer_id_ = 0;
    int64_t last_read_ms_ = 0;
    int64_t last_write_progress_ms_ = 0;
    bool closed_ = false;
    bool close_after_send_ = false;
};

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SRC_TCP_SESSION_H_
