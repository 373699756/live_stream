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
    NetConnectionInfo GetInfo() const;
    ConnectionId id() const { return id_; }
    NetAddress peer() const { return peer_; }

private:
    static constexpr size_t kInlineSliceBytes = 64;

    // OutSlice 是写队列里的最小发送单元。data 只允许指向三类稳定内存：
    // inline_data、heap_data，或由 buffer 持有的媒体 payload。
    struct OutSlice {
        OutSlice() = default;
        OutSlice(OutSlice &&other) noexcept;
        OutSlice &operator=(OutSlice &&other) noexcept;
        OutSlice(const OutSlice &) = delete;
        OutSlice &operator=(const OutSlice &) = delete;

        const uint8_t *data = nullptr;
        size_t size = 0;
        size_t offset = 0;
        MediaBufferRef buffer;
        std::array<uint8_t, kInlineSliceBytes> inline_data{};
        std::unique_ptr<uint8_t[]> heap_data;
    };

    // OutBuffer 对应一次 SendSlices() 调用。current_slice/offset 记录短写进度，
    // enqueue_ms 用于 send stall 检测，size 计入 pending_bytes_。
    struct OutBuffer {
        std::array<OutSlice, kMaxNetBufferSlices> slices{};
        size_t slice_count = 0;
        size_t current_slice = 0;
        uint32_t size = 0;
        int64_t enqueue_ms = 0;
    };

    bool BuildOutBuffer(const NetBufferSlices &slices, OutBuffer *buffer) const;
    bool EnqueueOutBuffer(OutBuffer buffer);
    void HandleEvents(uint32_t events);
    void HandleRead();
    void HandleWrite();
    void EnableWrite();
    void DisableWrite();
    void CloseInLoop(TcpCloseReason reason);
    void ArmTimeoutTimer();
    void CheckTimeouts();
    bool IsReadTimedOutLocked(int64_t now_ms) const;
    bool IsWriteTimedOutLocked(int64_t now_ms) const;
    bool IsSendStalledLocked() const;
    uint32_t TimeoutCheckIntervalMs() const;

    NetEngineImpl *engine_ = nullptr;
    std::shared_ptr<EventLoop> loop_;
    UniqueFd fd_;
    ConnectionId id_ = 0;
    TcpListenOptions options_;
    TcpCallbacks callbacks_;
    NetAddress local_;
    NetAddress peer_;
    mutable std::mutex mutex_;
    // send_queue_ 只能由 IO loop 写出，但 SendSlices()/GetInfo() 可能来自
    // 协议线程，因此队列和 pending_bytes_ 都由 mutex_ 保护。
    std::deque<OutBuffer> send_queue_;
    // pending_bytes_ 是 net 对慢客户端的统一背压指标，上层协议不应再维护
    // 独立 socket 写队列。
    uint32_t pending_bytes_ = 0;
    // timeout timer 周期检查 read/write/stall timeout。CloseInLoop() 会先取消它，
    // 避免 session 已关闭后还有 timer 回调访问状态。
    event::TimerId timeout_timer_id_ = 0;
    int64_t last_read_ms_ = 0;
    int64_t last_write_progress_ms_ = 0;
    TcpCloseReason close_reason_ = TcpCloseReason::kNormal;
    bool closed_ = false;
    // close_after_send_ 表示“排空当前队列后关闭”，用于 HTTP/RTSP 短响应；
    // 设置后不再接收新的 SendSlices()。
    bool close_after_send_ = false;
};

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SRC_TCP_SESSION_H_
