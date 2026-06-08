#include "tcp_session.h"

#include "infra/time.h"
#include "net_engine_impl.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace live_stream {
namespace net_internal {
namespace {

constexpr uint32_t kReadBufferSize = 4096;
constexpr uint32_t kDefaultManagerTickMs = 1000;

void RefNetBufferOwner(const NetBufferOwner &owner) {
    if (owner.ptr != nullptr && owner.ref != nullptr) {
        // owner 可能是 VideoBuffer。这里统一走协议无关的 ref 回调。
        owner.ref(owner.ptr);
    }
}

void UnrefNetBufferOwner(const NetBufferOwner &owner) {
    if (owner.ptr != nullptr && owner.unref != nullptr) {
        owner.unref(owner.ptr);
    }
}

}  // namespace

TcpSession::OutSlice::OutSlice(OutSlice&& other) noexcept
    : data(other.data),
      size(other.size),
      offset(other.offset),
      owner(other.owner),
      inline_data(other.inline_data),
      heap_data(std::move(other.heap_data)) {
    if (data == other.inline_data.data()) {
        data = inline_data.data();
    }
    other.data = nullptr;
    other.size = 0;
    other.offset = 0;
    other.owner = NetBufferOwner{};
}

TcpSession::OutSlice& TcpSession::OutSlice::operator=(
    OutSlice&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    UnrefNetBufferOwner(owner);
    data = other.data;
    size = other.size;
    offset = other.offset;
    owner = other.owner;
    inline_data = other.inline_data;
    heap_data = std::move(other.heap_data);
    if (data == other.inline_data.data()) {
        data = inline_data.data();
    }
    other.data = nullptr;
    other.size = 0;
    other.offset = 0;
    other.owner = NetBufferOwner{};
    return *this;
}

TcpSession::OutSlice::~OutSlice() {
    UnrefNetBufferOwner(owner);
}

TcpSession::TcpSession(NetEngineImpl *engine,
                       std::shared_ptr<EventLoop> loop, int fd,
                       ConnectionId id, const TcpListenOptions &options,
                       TcpCallbacks callbacks, NetAddress local,
                       NetAddress peer)
    : engine_(engine),
      loop_(std::move(loop)),
      fd_(fd),
      id_(id),
      options_(options),
      callbacks_(callbacks),
      local_(std::move(local)),
      peer_(std::move(peer)) {}

TcpSession::~TcpSession() { fd_.Reset(); }

bool TcpSession::Start() {
    const int64_t now_ms = infra::Time::MonotonicMillis();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_read_ms_ = now_ms;
        last_write_progress_ms_ = now_ms;
    }
    std::weak_ptr<TcpSession> weak_self = shared_from_this();
    if (!loop_->AddFd(fd_.get(), EPOLLIN, [weak_self](uint32_t events) {
        auto self = weak_self.lock();
        if (self) {
            self->HandleEvents(events);
        }
    })) {
        return false;
    }
    ArmManagerTimer();
    return true;
}

bool TcpSession::Send(const uint8_t *data, size_t size) {
    NetBufferSlices slices;
    if (!slices.Add(data, size)) {
        return false;
    }
    return SendSlices(slices);
}

bool TcpSession::SendSlices(const NetBufferSlices &slices) {
    const size_t total_size = slices.TotalSize();
    if (total_size > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    if (total_size == 0) {
        return true;
    }
    // 发送队列只能保存稳定内存：媒体 payload 用 NetBufferOwner 延长生命周期，
    // 其他小块/无 owner 数据在入队前复制，避免调用方栈内存跨线程悬空。
    OutBuffer buffer;
    if (!BuildOutBuffer(slices, &buffer)) {
        return false;
    }
    TcpCloseReason close_reason = TcpCloseReason::kNormal;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || close_after_send_) {
            return false;
        }
        // 队列项数和 pending bytes 是慢客户端的硬边界。命中后直接关闭连接，
        // 让 HTTP-FLV/RTSP 等热路径释放 reader 或媒体 buffer 引用。
        if (send_queue_.size() >= options_.send_queue_capacity) {
            engine_->AddSendBusy();
            close_reason = TcpCloseReason::kQueueFull;
        } else if (pending_bytes_ >= options_.send_buffer_limit_bytes ||
                   buffer.size >
                       options_.send_buffer_limit_bytes - pending_bytes_) {
            engine_->AddSendBusy();
            close_reason = TcpCloseReason::kPendingLimit;
        }
        if (close_reason == TcpCloseReason::kNormal) {
            buffer.enqueue_ms = infra::Time::MonotonicMillis();
            if (pending_bytes_ == 0) {
                last_write_progress_ms_ = buffer.enqueue_ms;
            }
            pending_bytes_ += buffer.size;
            send_queue_.push_back(std::move(buffer));
        }
    }
    if (close_reason != TcpCloseReason::kNormal) {
        engine_->AddSlowClose();
        (void)Close(close_reason);
        return false;
    }
    std::weak_ptr<TcpSession> weak_self = shared_from_this();
    const bool posted = loop_->Post([weak_self]() {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->EnableWrite();
        self->HandleWrite();
    });
    if (!posted) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!send_queue_.empty()) {
            pending_bytes_ -= send_queue_.back().size;
            send_queue_.pop_back();
        }
    }
    return posted;
}

bool TcpSession::Close(TcpCloseReason reason) {
    std::weak_ptr<TcpSession> weak_self = shared_from_this();
    return loop_->Post([weak_self, reason]() {
        auto self = weak_self.lock();
        if (self) {
            self->CloseInLoop(reason);
        }
    });
}

bool TcpSession::CloseAfterSend() {
    std::weak_ptr<TcpSession> weak_self = shared_from_this();
    return loop_->Post([weak_self]() {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        bool close_now = false;
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            if (self->closed_) {
                return;
            }
            // HTTP/RTSP 短响应需要先把已入队数据写完再关 socket；
            // close_after_send_ 只影响当前队列，不再接受新的发送。
            self->close_after_send_ = true;
            close_now = self->send_queue_.empty();
        }
        if (close_now) {
            self->CloseInLoop(TcpCloseReason::kNormal);
        } else {
            self->EnableWrite();
        }
    });
}

uint32_t TcpSession::PendingBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_bytes_;
}

NetConnectionDiagnostics TcpSession::Diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    NetConnectionDiagnostics diagnostics;
    diagnostics.connection_id = id_;
    diagnostics.owner_protocol = options_.owner_protocol;
    diagnostics.remote_address = peer_;
    diagnostics.local_address = local_;
    diagnostics.pending_bytes = pending_bytes_;
    diagnostics.send_queue_length = static_cast<uint32_t>(send_queue_.size());
    diagnostics.last_write_at_ms = last_write_progress_ms_;
    diagnostics.close_reason = close_reason_;
    diagnostics.open = !closed_;
    return diagnostics;
}

void TcpSession::HandleEvents(uint32_t events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0) {
        CloseInLoop(TcpCloseReason::kInternalError);
        return;
    }
    if ((events & EPOLLIN) != 0) {
        HandleRead();
    }
    if ((events & EPOLLOUT) != 0) {
        HandleWrite();
    }
}

void TcpSession::HandleRead() {
    uint8_t buffer[kReadBufferSize];
    while (true) {
        const ssize_t n = recv(fd_.get(), buffer, sizeof(buffer), 0);
        if (n > 0) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                last_read_ms_ = infra::Time::MonotonicMillis();
            }
            engine_->AddRead(static_cast<size_t>(n));
            engine_->DispatchRead(callbacks_, id_, buffer, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            CloseInLoop(TcpCloseReason::kRemoteClose);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        CloseInLoop(TcpCloseReason::kInternalError);
        return;
    }
}

void TcpSession::HandleWrite() {
    while (true) {
        ssize_t n = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || send_queue_.empty()) {
                break;
            }
            // send_queue_ 保存分片视图和每片偏移，短写时只推进 offset，
            // 下一次 EPOLLOUT 从同一片继续，pending_bytes_ 始终反映未写字节。
            OutBuffer &current = send_queue_.front();
            while (current.current_slice < current.slice_count &&
                   current.slices[current.current_slice].offset >=
                       current.slices[current.current_slice].size) {
                ++current.current_slice;
            }
            if (current.current_slice >= current.slice_count) {
                send_queue_.pop_front();
                continue;
            }
            OutSlice &slice = current.slices[current.current_slice];
            const uint8_t *data = slice.data + slice.offset;
            const size_t remain = slice.size - slice.offset;
            n = send(fd_.get(), data, remain, MSG_NOSIGNAL);
            if (n > 0) {
                slice.offset += static_cast<size_t>(n);
                pending_bytes_ -= static_cast<uint32_t>(n);
                last_write_progress_ms_ = infra::Time::MonotonicMillis();
                if (slice.offset >= slice.size) {
                    ++current.current_slice;
                }
                if (current.current_slice >= current.slice_count) {
                    send_queue_.pop_front();
                }
            }
        }
        if (n > 0) {
            engine_->AddWrite(static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            EnableWrite();
            return;
        }
        CloseInLoop(TcpCloseReason::kInternalError);
        return;
    }
    bool close_now = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        close_now = close_after_send_ && send_queue_.empty();
    }
    if (close_now) {
        CloseInLoop(TcpCloseReason::kNormal);
        return;
    }
    DisableWrite();
}

void TcpSession::EnableWrite() {
    if (fd_.valid()) {
        (void)loop_->ModifyFd(fd_.get(), EPOLLIN | EPOLLOUT);
    }
}

void TcpSession::DisableWrite() {
    if (fd_.valid()) {
        (void)loop_->ModifyFd(fd_.get(), EPOLLIN);
    }
}

void TcpSession::CloseInLoop(TcpCloseReason reason) {
    NetTimerId manager_timer_id = 0;
    NetConnectionDiagnostics diagnostics;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        diagnostics.connection_id = id_;
        diagnostics.owner_protocol = options_.owner_protocol;
        diagnostics.remote_address = peer_;
        diagnostics.local_address = local_;
        diagnostics.pending_bytes = pending_bytes_;
        diagnostics.send_queue_length =
            static_cast<uint32_t>(send_queue_.size());
        diagnostics.last_write_at_ms = last_write_progress_ms_;
        diagnostics.close_reason = reason;
        diagnostics.open = false;
        closed_ = true;
        close_reason_ = reason;
        manager_timer_id = manager_timer_id_;
        manager_timer_id_ = 0;
        send_queue_.clear();
        pending_bytes_ = 0;
    }
    if (manager_timer_id != 0) {
        loop_->CancelTimer(manager_timer_id);
    }
    if (fd_.valid()) {
        loop_->RemoveFd(fd_.get());
    }
    fd_.Reset();
    engine_->OnConnectionClosed(id_, callbacks_, reason, diagnostics);
}

void TcpSession::ArmManagerTimer() {
    const uint32_t tick_ms = ManagerTickMs();
    if (tick_ms == 0) {
        return;
    }
    std::weak_ptr<TcpSession> weak_self = shared_from_this();
    const NetTimerId timer_id = loop_->RunEvery(
        engine_->AllocateTimerId(), tick_ms, [weak_self]() {
            auto self = weak_self.lock();
            if (self) {
                self->CheckTimeouts();
            }
        });
    if (timer_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        loop_->CancelTimer(timer_id);
        return;
    }
    manager_timer_id_ = timer_id;
}

void TcpSession::CheckTimeouts() {
    TcpCloseReason reason = TcpCloseReason::kNormal;
    bool should_close = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        const int64_t now_ms = infra::Time::MonotonicMillis();
        // read timeout 保护空闲控制连接；write timeout/send stall 保护媒体长连接
        // 或 RTSP interleaved 慢客户端，关闭原因会进入 net diagnostics。
        if (IsReadTimedOutLocked(now_ms)) {
            reason = TcpCloseReason::kReadTimeout;
            should_close = true;
        } else if (IsWriteTimedOutLocked(now_ms)) {
            reason = TcpCloseReason::kWriteTimeout;
            should_close = true;
        } else if (IsSendStalledLocked()) {
            reason = TcpCloseReason::kSendStall;
            should_close = true;
        }
    }
    if (!should_close) {
        return;
    }
    if (reason == TcpCloseReason::kSendStall ||
        reason == TcpCloseReason::kWriteTimeout) {
        engine_->AddSlowClose();
    }
    CloseInLoop(reason);
}

bool TcpSession::IsReadTimedOutLocked(int64_t now_ms) const {
    return options_.read_timeout_ms != 0 &&
           now_ms - last_read_ms_ >=
               static_cast<int64_t>(options_.read_timeout_ms);
}

bool TcpSession::IsWriteTimedOutLocked(int64_t now_ms) const {
    return options_.write_timeout_ms != 0 && pending_bytes_ > 0 &&
           now_ms - last_write_progress_ms_ >=
               static_cast<int64_t>(options_.write_timeout_ms);
}

bool TcpSession::IsSendStalledLocked() const {
    if (options_.send_stall_timeout_ms == 0 || send_queue_.empty()) {
        return false;
    }
    const int64_t age_ms =
        infra::Time::MonotonicMillis() - send_queue_.front().enqueue_ms;
    return age_ms >= static_cast<int64_t>(options_.send_stall_timeout_ms);
}

uint32_t TcpSession::ManagerTickMs() const {
    uint32_t tick_ms = 0;
    if (options_.read_timeout_ms != 0) {
        tick_ms = options_.read_timeout_ms;
    }
    if (options_.write_timeout_ms != 0 &&
        (tick_ms == 0 || options_.write_timeout_ms < tick_ms)) {
        tick_ms = options_.write_timeout_ms;
    }
    if (options_.send_stall_timeout_ms != 0 &&
        (tick_ms == 0 || options_.send_stall_timeout_ms < tick_ms)) {
        tick_ms = options_.send_stall_timeout_ms;
    }
    if (tick_ms == 0) {
        return 0;
    }
    if (tick_ms > kDefaultManagerTickMs) {
        return kDefaultManagerTickMs;
    }
    return tick_ms;
}

bool TcpSession::BuildOutBuffer(const NetBufferSlices &slices,
                                OutBuffer *buffer) const {
    if (buffer == nullptr || slices.count > kMaxNetBufferSlices) {
        return false;
    }
    *buffer = OutBuffer{};
    size_t total_size = 0;
    for (size_t i = 0; i < slices.count; ++i) {
        const NetBufferSlice &input = slices.slices[i];
        if (input.size == 0) {
            continue;
        }
        if (input.data == nullptr || buffer->slice_count >= kMaxNetBufferSlices ||
            input.size > std::numeric_limits<uint32_t>::max() - total_size) {
            return false;
        }
        OutSlice &out = buffer->slices[buffer->slice_count];
        out.size = input.size;
        out.owner = input.owner;
        if (out.owner.ptr != nullptr) {
            // owner 由 media_source/media buffer 提供。这里加引用后，异步发送期间
            // payload 不需要复制；OutSlice 析构时会 unref。
            RefNetBufferOwner(out.owner);
            out.data = input.data;
        } else if (input.size <= out.inline_data.size()) {
            std::memcpy(out.inline_data.data(), input.data, input.size);
            out.data = out.inline_data.data();
        } else {
            out.heap_data.reset(new (std::nothrow) uint8_t[input.size]);
            if (!out.heap_data) {
                return false;
            }
            std::memcpy(out.heap_data.get(), input.data, input.size);
            out.data = out.heap_data.get();
        }
        total_size += input.size;
        ++buffer->slice_count;
    }
    if (buffer->slice_count == 0 ||
        total_size > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    buffer->size = static_cast<uint32_t>(total_size);
    return true;
}

}  // namespace net_internal
}  // namespace live_stream
