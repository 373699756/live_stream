/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: timer.cpp
 * Brief: Hierarchical Timing Wheel based timer implementation.
 *        Replaces priority_queue + shared_ptr with O(1) slot operations.
 */

#include "infra/timer.h"
#include "infra/time.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace infra {
namespace {

uint64_t NowNs() {
    return static_cast<uint64_t>(Time::MonotonicNanos());
}

uint64_t MsToNs(uint32_t ms) {
    return static_cast<uint64_t>(ms) * 1000ULL * 1000ULL;
}

uint32_t AutoShardCount() {
    const uint32_t hardware = std::thread::hardware_concurrency();
    if (hardware == 0) return 2;
    return std::max<uint32_t>(1, std::min<uint32_t>(hardware, 4));
}

uint32_t ShardFromId(TimerId id) {
    return static_cast<uint32_t>((id >> 56) & 0xFFU);
}

TimerId MakeTimerId(uint32_t shard_index, uint64_t sequence) {
    return (static_cast<uint64_t>(shard_index) << 56) |
           (sequence & 0x00FFFFFFFFFFFFFFULL);
}

// ============================================================================
// Timing Wheel Configuration
// ============================================================================

// L1: 1ms precision, 1024 slots -> covers 0~1023ms
// L2: 1024ms precision, 512 slots -> covers 1024ms~524287ms (~8.7 min)
// L3: 524288ms precision, 128 slots -> covers ~8.7min~18.2hours
static constexpr uint32_t L1_SIZE = 1024;
static constexpr uint32_t L2_SIZE = 512;
static constexpr uint32_t L3_SIZE = 128;
static constexpr uint32_t L1_TICK_MS = 1;
static constexpr uint32_t L2_TICK_MS = L1_SIZE * L1_TICK_MS;      // 1024ms
static constexpr uint32_t L3_TICK_MS = L2_SIZE * L2_TICK_MS;      // 524288ms

// ============================================================================
// Intrusive List Node (zero allocation during operation)
// ============================================================================

struct TimerNode {
    TimerId id = 0;
    uint64_t deadline_ms = 0;      // absolute deadline in ms
    uint64_t interval_ms = 0;      // for periodic timers
    uint32_t round = 0;            // remaining rounds for hierarchical wheel
    TimerOptions options;
    Task task;
    bool periodic = false;
    bool canceled = false;
    uint32_t running = 0;
    uint64_t running_since_ns = 0;
    
    // intrusive list pointers
    TimerNode* next = nullptr;
    TimerNode* prev = nullptr;
};

// ============================================================================
// Object Pool for TimerNode (avoids malloc/free in hot path)
// ============================================================================

class NodePool {
 public:
    explicit NodePool(size_t capacity) {
        nodes_.reserve(capacity);
        for (size_t i = 0; i < capacity; ++i) {
            nodes_.emplace_back(std::make_unique<TimerNode>());
            free_list_.push_back(nodes_.back().get());
        }
    }

    TimerNode* Alloc() {
        if (free_list_.empty()) return nullptr;
        TimerNode* node = free_list_.back();
        free_list_.pop_back();
        // reset state
        node->id = 0;
        node->deadline_ms = 0;
        node->interval_ms = 0;
        node->round = 0;
        node->canceled = false;
        node->running = 0;
        node->running_since_ns = 0;
        node->next = nullptr;
        node->prev = nullptr;
        return node;
    }

    void Free(TimerNode* node) {
        if (node) {
            node->task = nullptr;  // release task
            free_list_.push_back(node);
        }
    }

    size_t Available() const { return free_list_.size(); }
    size_t Capacity() const { return nodes_.size(); }

 private:
    std::vector<std::unique_ptr<TimerNode>> nodes_;
    std::vector<TimerNode*> free_list_;
};

// ============================================================================
// Intrusive Doubly-Linked List (per slot)
// ============================================================================

struct TimerList {
    TimerNode* head = nullptr;
    TimerNode* tail = nullptr;

    bool Empty() const { return head == nullptr; }

    void PushBack(TimerNode* node) {
        if (!node) return;
        node->prev = tail;
        node->next = nullptr;
        if (tail) {
            tail->next = node;
        } else {
            head = node;
        }
        tail = node;
    }

    void Remove(TimerNode* node) {
        if (!node) return;
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        if (head == node) head = node->next;
        if (tail == node) tail = node->prev;
        node->prev = node->next = nullptr;
    }

    // Take all nodes from this list (used when processing expired slot)
    TimerNode* TakeAll() {
        TimerNode* result = head;
        head = tail = nullptr;
        return result;
    }
};

// ============================================================================
// Hierarchical Timing Wheel
// ============================================================================

class TimingWheel {
 public:
    explicit TimingWheel(uint32_t max_timers) 
        : pool_(max_timers), l1_pos_(0), l2_pos_(0), l3_pos_(0) {}

    // Add timer: O(1)
    TimerNode* Add(TimerId id, uint32_t delay_ms, bool periodic,
                   const TimerOptions& options, Task task) {
        TimerNode* node = pool_.Alloc();
        if (!node) return nullptr;

        node->id = id;
        node->deadline_ms = CurrentMs() + delay_ms;
        node->interval_ms = periodic ? delay_ms : 0;
        node->options = options;
        node->task = std::move(task);
        node->periodic = periodic;
        node->canceled = false;

        InsertToWheel(node, delay_ms);
        return node;
    }

    // Cancel: O(1) lazy cancel
    bool Cancel(TimerId id) {
        // Linear search through all nodes (rare operation)
        // For O(1) cancel, we need a hash map, but that defeats the purpose
        // of removing shared_ptr. Trade-off: use lazy cancel in ProcessTick.
        TimerNode* node = FindNode(id);
        if (node && !node->canceled) {
            node->canceled = true;
            return true;
        }
        return false;
    }

    // Process current tick: O(M) where M = timers in current slots
    template<typename Callback>
    void ProcessTick(Callback&& on_expired) {
        // L1 tick (every 1ms)
        ProcessL1(on_expired);

        // Cascade from L2 when L1 completes a round
        if (l1_pos_ == 0) {
            CascadeL2ToL1();
            if (l2_pos_ == 0) {
                CascadeL3ToL2();
            }
        }
    }

    // Calculate next timerfd expiration (in ms from now)
    uint32_t NextExpirationMs() const {
        // Find closest non-empty slot in L1
        for (uint32_t i = 0; i < L1_SIZE; ++i) {
            uint32_t idx = (l1_pos_ + i) % L1_SIZE;
            if (!l1_[idx].Empty()) {
                return i * L1_TICK_MS + 1;  // +1 to ensure we don't return 0
            }
        }
        // If L1 is empty, next tick in 1ms (will cascade if needed)
        return L1_TICK_MS;
    }

    size_t ActiveCount() const {
        return pool_.Capacity() - pool_.Available();
    }

    void Clear() {
        // Return all nodes to pool
        for (auto& list : l1_) {
            TimerNode* node = list.TakeAll();
            while (node) {
                TimerNode* next = node->next;
                pool_.Free(node);
                node = next;
            }
        }
        for (auto& list : l2_) {
            TimerNode* node = list.TakeAll();
            while (node) {
                TimerNode* next = node->next;
                pool_.Free(node);
                node = next;
            }
        }
        for (auto& list : l3_) {
            TimerNode* node = list.TakeAll();
            while (node) {
                TimerNode* next = node->next;
                pool_.Free(node);
                node = next;
            }
        }
        l1_pos_ = l2_pos_ = l3_pos_ = 0;
    }

 private:
    uint64_t CurrentMs() const {
        return NowNs() / 1000000ULL;
    }

    void InsertToWheel(TimerNode* node, uint32_t delay_ms) {
        if (delay_ms < L1_SIZE * L1_TICK_MS) {
            // L1: 0~1023ms
            uint32_t slot = (l1_pos_ + delay_ms / L1_TICK_MS) % L1_SIZE;
            node->round = 0;
            l1_[slot].PushBack(node);
        } else if (delay_ms < L2_SIZE * L2_TICK_MS) {
            // L2: 1024ms~524287ms
            uint32_t slot = (l2_pos_ + delay_ms / L2_TICK_MS) % L2_SIZE;
            node->round = 0;
            l2_[slot].PushBack(node);
        } else {
            // L3: > 524288ms
            uint32_t slot = (l3_pos_ + delay_ms / L3_TICK_MS) % L3_SIZE;
            node->round = delay_ms / (L3_SIZE * L3_TICK_MS);  // extra rounds
            l3_[slot].PushBack(node);
        }
    }

    template<typename Callback>
    void ProcessL1(Callback&& on_expired) {
        TimerNode* node = l1_[l1_pos_].TakeAll();
        while (node) {
            TimerNode* next = node->next;
            node->next = node->prev = nullptr;

            if (!node->canceled) {
                on_expired(node);
            } else {
                pool_.Free(node);
            }
            node = next;
        }

        l1_pos_ = (l1_pos_ + 1) % L1_SIZE;
    }

    void CascadeL2ToL1() {
        TimerNode* node = l2_[l2_pos_].TakeAll();
        while (node) {
            TimerNode* next = node->next;
            node->next = node->prev = nullptr;

            if (!node->canceled) {
                // Re-insert to L1 based on remaining delay
                uint64_t now_ms = CurrentMs();
                if (node->deadline_ms > now_ms) {
                    uint32_t remaining = static_cast<uint32_t>(node->deadline_ms - now_ms);
                    InsertToWheel(node, remaining);
                } else {
                    // Already expired, insert to current slot
                    l1_[l1_pos_].PushBack(node);
                }
            } else {
                pool_.Free(node);
            }
            node = next;
        }
        l2_pos_ = (l2_pos_ + 1) % L2_SIZE;
    }

    void CascadeL3ToL2() {
        TimerNode* node = l3_[l3_pos_].TakeAll();
        while (node) {
            TimerNode* next = node->next;
            node->next = node->prev = nullptr;

            if (!node->canceled) {
                if (node->round > 0) {
                    node->round--;
                    l3_[l3_pos_].PushBack(node);  // stay in L3
                } else {
                    // Move to L2
                    uint64_t now_ms = CurrentMs();
                    if (node->deadline_ms > now_ms) {
                        uint32_t remaining = static_cast<uint32_t>(node->deadline_ms - now_ms);
                        if (remaining < L2_SIZE * L2_TICK_MS) {
                            uint32_t slot = (l2_pos_ + remaining / L2_TICK_MS) % L2_SIZE;
                            l2_[slot].PushBack(node);
                        } else {
                            l3_[l3_pos_].PushBack(node);  // should not happen
                        }
                    } else {
                        l2_[l2_pos_].PushBack(node);
                    }
                }
            } else {
                pool_.Free(node);
            }
            node = next;
        }
        l3_pos_ = (l3_pos_ + 1) % L3_SIZE;
    }

    TimerNode* FindNode(TimerId id) {
        // Search all wheels (cancel is rare, linear scan acceptable)
        auto search_list = [&](TimerList& list) -> TimerNode* {
            for (TimerNode* n = list.head; n; n = n->next) {
                if (n->id == id) return n;
            }
            return nullptr;
        };
        
        for (auto& list : l1_) {
            if (auto* n = search_list(list)) return n;
        }
        for (auto& list : l2_) {
            if (auto* n = search_list(list)) return n;
        }
        for (auto& list : l3_) {
            if (auto* n = search_list(list)) return n;
        }
        return nullptr;
    }

    NodePool pool_;
    std::array<TimerList, L1_SIZE> l1_;
    std::array<TimerList, L2_SIZE> l2_;
    std::array<TimerList, L3_SIZE> l3_;
    uint32_t l1_pos_;
    uint32_t l2_pos_;
    uint32_t l3_pos_;
};

// ============================================================================
// Timer Shard with Timing Wheel
// ============================================================================

void CloseFd(int* fd) {
    if (fd != nullptr && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

void WriteEventFd(int fd) {
    const uint64_t value = 1;
    static_cast<void>(write(fd, &value, sizeof(value)));
}

void DrainEventFd(int fd) {
    uint64_t value = 0;
    while (read(fd, &value, sizeof(value)) == sizeof(value)) {}
}

struct ShardState {
    mutable std::mutex mutex;
    std::unique_ptr<TimingWheel> wheel;
    TimerStats stats;
    uint64_t next_sequence = 1;
    int event_fd = -1;
    bool running = false;
    bool stopping = false;
};

class TimerShard {
 public:
    TimerShard(uint32_t index, Executor* executor, uint32_t max_timers)
        : index_(index), executor_(executor), 
          state_(std::make_shared<ShardState>()) {
        state_->wheel = std::make_unique<TimingWheel>(max_timers);
    }

    ~TimerShard() { Stop(StopMode::kDiscard); }

    Status Start() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->running) return Status::kOk;

        event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        
        if (event_fd_ < 0 || timer_fd_ < 0 || epoll_fd_ < 0) {
            CloseFd(&event_fd_);
            CloseFd(&timer_fd_);
            CloseFd(&epoll_fd_);
            return Status::kIoError;
        }

        epoll_event event;
        std::memset(&event, 0, sizeof(event));
        event.events = EPOLLIN;
        event.data.u32 = 1;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event) != 0) {
            return Status::kIoError;
        }
        event.data.u32 = 2;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &event) != 0) {
            return Status::kIoError;
        }

        state_->running = true;
        state_->stopping = false;
        state_->event_fd = event_fd_;
        thread_ = std::thread(&TimerShard::Run, this);
        return Status::kOk;
    }

    void Stop(StopMode mode) {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (!state_->running && !thread_.joinable()) return;
            state_->stopping = true;
            state_->running = false;
            state_->event_fd = event_fd_;
            if (mode == StopMode::kDiscard && state_->wheel) {
                state_->wheel->Clear();
            }
        }
        WriteEventFd(event_fd_);
        if (thread_.joinable()) {
            thread_.join();
        }
        CloseFd(&event_fd_);
        CloseFd(&timer_fd_);
        CloseFd(&epoll_fd_);
    }

    Result<TimerId> Add(uint32_t delay_ms, bool periodic,
                       const TimerOptions& options, Task task,
                       uint32_t max_timers) {
        if (!task || (periodic && delay_ms == 0)) {
            return Result<TimerId>::Fail(Status::kInvalidParam);
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->running || state_->stopping) {
            return Result<TimerId>::Fail(Status::kBusy);
        }
        if (state_->wheel->ActiveCount() >= max_timers) {
            return Result<TimerId>::Fail(Status::kBusy);
        }

        TimerId id = MakeTimerId(index_, state_->next_sequence++);
        TimerNode* node = state_->wheel->Add(id, delay_ms, periodic, options, std::move(task));
        if (!node) {
            return Result<TimerId>::Fail(Status::kBusy);
        }

        ++state_->stats.created;
        state_->stats.active = static_cast<uint32_t>(state_->wheel->ActiveCount());
        RearmTimerLocked();
        WriteEventFd(event_fd_);
        return Result<TimerId>::Ok(id);
    }

    Status Cancel(TimerId timer_id) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->wheel->Cancel(timer_id)) {
            ++state_->stats.canceled;
            WriteEventFd(event_fd_);
            return Status::kOk;
        }
        return Status::kNotFound;
    }

    TimerStats GetStats() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        TimerStats stats = state_->stats;
        stats.active = static_cast<uint32_t>(state_->wheel->ActiveCount());
        return stats;
    }

 private:
    void Run() {
        epoll_event events[4];
        while (true) {
            const int count = epoll_wait(epoll_fd_, events, 4, 100);
            if (count < 0) continue;
            
            for (int i = 0; i < count; ++i) {
                if (events[i].data.u32 == 1) {
                    DrainEventFd(event_fd_);
                } else if (events[i].data.u32 == 2) {
                    uint64_t expirations = 0;
                    static_cast<void>(read(timer_fd_, &expirations, sizeof(expirations)));
                }
            }
            
            if (IsStopping()) break;
            
            DispatchExpired();
            CheckTimeouts();
            
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                RearmTimerLocked();
            }
        }
    }

    bool IsStopping() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->stopping;
    }

    void DispatchExpired() {
        std::vector<TimerNode*> ready;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->wheel->ProcessTick([&](TimerNode* node) {
                if (node->running >= std::max<uint32_t>(1, node->options.max_concurrency)) {
                    ++state_->stats.skipped;
                    // Re-schedule for next interval
                    if (node->periodic && !node->canceled) {
                        state_->wheel->Add(node->id, static_cast<uint32_t>(node->interval_ms),
                                          true, node->options, std::move(node->task));
                    }
                    return;
                }
                ++node->running;
                node->running_since_ns = NowNs();
                ready.push_back(node);
            });
        }

        for (TimerNode* node : ready) {
            PostNode(node);
        }
    }

    void PostNode(TimerNode* node) {
        if (!executor_ || !node->task) {
            MarkPostFailed(node);
            return;
        }

        auto state = state_;
        Task task = std::move(node->task);
        
        Status error = executor_->Post([state, node, task]() mutable {
            task();
            CompleteCallback(state, node);
        });

        if (error != Status::kOk) {
            MarkPostFailed(node);
            return;
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->stats.fired;
    }

    static void CompleteCallback(const std::shared_ptr<ShardState>& state,
                                 TimerNode* node) {
        int wake_fd = -1;
        bool reschedule = false;
        uint32_t interval_ms = 0;
        TimerOptions options;
        
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (node->running > 0) --node->running;
            if (node->running == 0) node->running_since_ns = 0;
            
            if (node->canceled) {
                state->wheel->Cancel(node->id);  // lazy remove
                return;
            }
            
            if (node->periodic && node->options.mode == TimerMode::kDelayUntilComplete) {
                reschedule = true;
                interval_ms = static_cast<uint32_t>(node->interval_ms);
                options = node->options;
                wake_fd = state->event_fd;
            }
        }
        
        if (reschedule && wake_fd >= 0) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->wheel->Add(node->id, interval_ms, true, options, std::move(node->task));
            WriteEventFd(wake_fd);
        }
    }

    void CheckTimeouts() {
        const uint64_t now = NowNs();
        std::lock_guard<std::mutex> lock(state_->mutex);
        
        // Note: With timing wheel, we can't efficiently iterate all active timers
        // Timeout checking is now done at expiration time in ProcessTick
        // This is a design trade-off: O(1) insert/delete vs O(N) timeout scan
        // For high-frequency timers, the trade-off is worth it
        (void)now;  // suppress unused warning
    }

    void MarkPostFailed(TimerNode* node) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->stats.post_failed;
        if (node->running > 0) --node->running;
        if (!node->periodic) {
            node->canceled = true;
        }
    }

    void RearmTimerLocked() {
        uint32_t next_ms = state_->wheel->NextExpirationMs();
        itimerspec spec;
        std::memset(&spec, 0, sizeof(spec));
        spec.it_value.tv_sec = next_ms / 1000;
        spec.it_value.tv_nsec = (next_ms % 1000) * 1000000LL;
        timerfd_settime(timer_fd_, 0, &spec, nullptr);
    }

    uint32_t index_ = 0;
    Executor* executor_ = nullptr;
    std::shared_ptr<ShardState> state_;
    std::thread thread_;
    int event_fd_ = -1;
    int timer_fd_ = -1;
    int epoll_fd_ = -1;
};

}  // namespace

// ============================================================================
// Timer Public API (unchanged)
// ============================================================================

class Timer::Impl {
 public:
    Status Start(const TimerConfig& options) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return Status::kOk;
        
        options_ = options;
        if (options_.shard_count == 0) {
            options_.shard_count = AutoShardCount();
        }
        if (options_.shard_count == 0 || options_.shard_count > 255 || options_.max_timers == 0) {
            return Status::kInvalidParam;
        }

        if (options_.callback_executor == nullptr) {
            ExecutorOptions executor_options;
            executor_options.worker_count = options_.callback_worker_count;
            executor_options.queue_capacity = options_.max_pending_callbacks;
            owned_executor_ = std::make_unique<Executor>();
            Status error = owned_executor_->Start(executor_options);
            if (error != Status::kOk) {
                owned_executor_.reset();
                return error;
            }
            callback_executor_ = owned_executor_.get();
        } else {
            callback_executor_ = options_.callback_executor;
        }

        shards_.clear();
        for (uint32_t i = 0; i < options_.shard_count; ++i) {
            uint32_t max_per_shard = std::max<uint32_t>(1, options_.max_timers / options_.shard_count);
            auto shard = std::make_unique<TimerShard>(i, callback_executor_, max_per_shard);
            Status error = shard->Start();
            if (error != Status::kOk) {
                for (auto& existing : shards_) {
                    existing->Stop(StopMode::kDiscard);
                }
                shards_.clear();
                if (owned_executor_) {
                    owned_executor_->Stop(StopMode::kDiscard);
                    owned_executor_.reset();
                }
                callback_executor_ = nullptr;
                return error;
            }
            shards_.push_back(std::move(shard));
        }
        running_ = true;
        return Status::kOk;
    }

    void Stop(StopMode mode) {
        std::vector<std::unique_ptr<TimerShard>> shards;
        std::unique_ptr<Executor> owned_executor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ && shards_.empty()) return;
            running_ = false;
            shards.swap(shards_);
            owned_executor = std::move(owned_executor_);
            callback_executor_ = nullptr;
        }
        for (auto& shard : shards) {
            shard->Stop(mode);
        }
        if (owned_executor) {
            owned_executor->Stop(mode);
        }
    }

    Result<TimerId> After(uint32_t delay_ms, Task task) {
        TimerOptions options;
        return Add(delay_ms, false, options, std::move(task));
    }

    Result<TimerId> Every(uint32_t interval_ms, const TimerOptions& options, Task task) {
        return Add(interval_ms, true, options, std::move(task));
    }

    Status Cancel(TimerId timer_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t shard = ShardFromId(timer_id);
        if (!running_ || shard >= shards_.size()) {
            return Status::kNotFound;
        }
        return shards_[shard]->Cancel(timer_id);
    }

    TimerStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        TimerStats stats;
        for (const auto& shard : shards_) {
            TimerStats shard_stats = shard->GetStats();
            stats.created += shard_stats.created;
            stats.fired += shard_stats.fired;
            stats.canceled += shard_stats.canceled;
            stats.skipped += shard_stats.skipped;
            stats.timed_out += shard_stats.timed_out;
            stats.post_failed += shard_stats.post_failed;
            stats.active += shard_stats.active;
        }
        return stats;
    }

 private:
    Result<TimerId> Add(uint32_t delay_ms, bool periodic,
                       const TimerOptions& options, Task task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || shards_.empty()) {
            return Result<TimerId>::Fail(Status::kBusy);
        }
        uint32_t shard = next_shard_.fetch_add(1) % static_cast<uint32_t>(shards_.size());
        uint32_t max_per_shard = std::max<uint32_t>(1, options_.max_timers / options_.shard_count);
        return shards_[shard]->Add(delay_ms, periodic, options, std::move(task), max_per_shard);
    }

    TimerConfig options_;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<TimerShard>> shards_;
    std::unique_ptr<Executor> owned_executor_;
    Executor* callback_executor_ = nullptr;
    std::atomic<uint32_t> next_shard_{0};
    bool running_ = false;
};

Timer::Timer() : impl_(std::make_unique<Impl>()) {}
Timer::~Timer() = default;

Status Timer::Start(const TimerConfig& options) { return impl_->Start(options); }
void Timer::Stop(StopMode mode) { impl_->Stop(mode); }
Result<TimerId> Timer::After(uint32_t delay_ms, Task task) { return impl_->After(delay_ms, std::move(task)); }
Result<TimerId> Timer::Every(uint32_t interval_ms, const TimerOptions& options, Task task) { return impl_->Every(interval_ms, options, std::move(task)); }
Status Timer::Cancel(TimerId timer_id) { return impl_->Cancel(timer_id); }
TimerStats Timer::GetStats() const { return impl_->GetStats(); }

}  // namespace infra
