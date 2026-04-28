/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: timer.cpp
 * Brief: Implements the infra asynchronous timer service.
 */

#include "infra/timer.h"
#include "infra/time.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace infra {
namespace {

constexpr uint32_t kMaxShardCount = 255;
constexpr uint32_t kShardShift = 56;
constexpr uint32_t kIndexShift = 32;
constexpr uint64_t kGenerationMask = 0xFFFFFFFFULL;
constexpr uint32_t kIndexMask = 0x00FFFFFFU;

uint64_t NowNs() {
  return static_cast<uint64_t>(Time::MonotonicNanos());
}

uint64_t MsToNs(uint32_t ms) {
  return static_cast<uint64_t>(ms) * 1000ULL * 1000ULL;
}

uint32_t AutoShardCount() {
  const uint32_t hardware = std::thread::hardware_concurrency();
  if (hardware == 0) {
    return 2;
  }
  return std::max<uint32_t>(1, std::min<uint32_t>(hardware, 4));
}

uint32_t ShardFromId(TimerId id) {
  return static_cast<uint32_t>((id >> kShardShift) & 0xFFU);
}

uint32_t IndexFromId(TimerId id) {
  return static_cast<uint32_t>((id >> kIndexShift) & kIndexMask);
}

uint32_t GenerationFromId(TimerId id) {
  return static_cast<uint32_t>(id & kGenerationMask);
}

TimerId MakeTimerId(uint32_t shard_index, uint32_t slot_index,
                    uint32_t generation) {
  return (static_cast<uint64_t>(shard_index) << kShardShift) |
         (static_cast<uint64_t>(slot_index & kIndexMask) << kIndexShift) |
         generation;
}

bool IsValidOptions(bool periodic, uint32_t delay_ms,
                    const TimerOptions& options, const Task& task) {
  if (!task) {
    return false;
  }
  if (periodic && delay_ms == 0) {
    return false;
  }
  if (options.max_concurrency == 0) {
    return false;
  }
  return true;
}

struct TaskHolder {
  explicit TaskHolder(Task task_value) : task(std::move(task_value)) {}

  Task task;
};

struct TimerEntry {
  TimerId id = 0;
  uint32_t generation = 0;
  uint64_t deadline_ns = 0;
  uint64_t interval_ns = 0;
  TimerOptions options;
  std::shared_ptr<TaskHolder> holder;
  bool allocated = false;
  bool active = false;
  bool periodic = false;
  uint32_t running = 0;
};

struct HeapItem {
  uint64_t deadline_ns = 0;
  TimerId id = 0;

  bool operator>(const HeapItem& other) const {
    if (deadline_ns != other.deadline_ns) {
      return deadline_ns > other.deadline_ns;
    }
    return id > other.id;
  }
};

struct ShardState {
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::vector<TimerEntry> entries;
  std::vector<uint32_t> free_indices;
  std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>>
      heap;
  TimerStats stats;
  bool running = false;
  bool stopping = false;
};

void PushTimerLocked(ShardState* state, TimerEntry* entry,
                     uint64_t deadline_ns) {
  entry->deadline_ns = deadline_ns;
  state->heap.push(HeapItem{deadline_ns, entry->id});
}

TimerEntry* FindEntryLocked(ShardState* state, TimerId id) {
  const uint32_t index = IndexFromId(id);
  if (index >= state->entries.size()) {
    return nullptr;
  }
  TimerEntry& entry = state->entries[index];
  if (!entry.allocated || entry.id != id ||
      entry.generation != GenerationFromId(id)) {
    return nullptr;
  }
  return &entry;
}

void FreeEntryLocked(ShardState* state, TimerEntry* entry) {
  if (entry == nullptr || !entry->allocated || entry->active ||
      entry->running != 0) {
    return;
  }

  const uint32_t index = IndexFromId(entry->id);
  entry->id = 0;
  entry->deadline_ns = 0;
  entry->interval_ns = 0;
  entry->holder.reset();
  entry->active = false;
  entry->periodic = false;
  entry->allocated = false;
  state->free_indices.push_back(index);
}

uint32_t ActiveCountLocked(const ShardState& state) {
  uint32_t count = 0;
  for (const TimerEntry& entry : state.entries) {
    if (entry.allocated) {
      ++count;
    }
  }
  return count;
}

class TimerShard {
 public:
  TimerShard(uint32_t index, Executor* executor, uint32_t max_timers)
      : index_(index), executor_(executor), max_timers_(max_timers),
        state_(new ShardState()) {}

  ~TimerShard() { Stop(StopMode::kDiscard); }

  Status Start() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->running) {
      return Status::kOk;
    }

    state_->entries.clear();
    state_->free_indices.clear();
    while (!state_->heap.empty()) {
      state_->heap.pop();
    }
    state_->entries.resize(max_timers_);
    state_->free_indices.reserve(max_timers_);
    for (uint32_t i = 0; i < max_timers_; ++i) {
      state_->entries[i].generation = 1;
      state_->free_indices.push_back(max_timers_ - 1U - i);
    }
    state_->stats = TimerStats{};
    state_->stopping = false;
    state_->running = true;
    thread_ = std::thread(&TimerShard::Run, this);
    return Status::kOk;
  }

  void Stop(StopMode mode) {
    (void)mode;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (!state_->running && !thread_.joinable()) {
        return;
      }
      state_->stopping = true;
      state_->running = false;
      DiscardLocked();
    }
    state_->condition.notify_all();

    if (thread_.joinable()) {
      thread_.join();
    }

    std::lock_guard<std::mutex> lock(state_->mutex);
    DiscardLocked();
  }

  Result<TimerId> Add(uint32_t delay_ms, bool periodic,
                      const TimerOptions& options, Task task) {
    if (!IsValidOptions(periodic, delay_ms, options, task)) {
      return Result<TimerId>::Fail(Status::kInvalidParam);
    }

    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->running || state_->stopping) {
      return Result<TimerId>::Fail(Status::kBusy);
    }
    if (state_->free_indices.empty()) {
      return Result<TimerId>::Fail(Status::kBusy);
    }

    std::shared_ptr<TaskHolder> holder(new TaskHolder(std::move(task)));
    const uint32_t slot_index = state_->free_indices.back();
    state_->free_indices.pop_back();
    TimerEntry& entry = state_->entries[slot_index];
    entry.generation += 1;
    if (entry.generation == 0) {
      entry.generation = 1;
    }
    entry.id = MakeTimerId(index_, slot_index, entry.generation);
    entry.interval_ns = periodic ? MsToNs(delay_ms) : 0;
    entry.options = options;
    entry.holder = holder;
    entry.allocated = true;
    entry.active = true;
    entry.periodic = periodic;
    entry.running = 0;
    PushTimerLocked(state_.get(), &entry, NowNs() + MsToNs(delay_ms));
    ++state_->stats.created;
    state_->stats.active = ActiveCountLocked(*state_);
    state_->condition.notify_all();
    return Result<TimerId>::Ok(entry.id);
  }

  Status Cancel(TimerId timer_id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    TimerEntry* entry = FindEntryLocked(state_.get(), timer_id);
    if (entry == nullptr || !entry->active) {
      return Status::kNotFound;
    }

    entry->active = false;
    ++state_->stats.canceled;
    FreeEntryLocked(state_.get(), entry);
    state_->stats.active = ActiveCountLocked(*state_);
    state_->condition.notify_all();
    return Status::kOk;
  }

  TimerStats GetStats() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    TimerStats stats = state_->stats;
    stats.active = ActiveCountLocked(*state_);
    return stats;
  }

 private:
  struct ReadyTimer {
    TimerId id = 0;
    uint64_t started_ns = 0;
    uint32_t timeout_ms = 0;
    std::shared_ptr<TaskHolder> holder;
  };

  void Run() {
    std::unique_lock<std::mutex> lock(state_->mutex);
    while (true) {
      DropStaleHeapItemsLocked();
      if (state_->stopping) {
        if (!HasRunnableWorkLocked()) {
          break;
        }
        if (state_->heap.empty()) {
          state_->condition.wait(lock);
          continue;
        }
      }

      if (state_->heap.empty()) {
        state_->condition.wait(lock);
        continue;
      }

      const uint64_t now_ns = NowNs();
      const uint64_t deadline_ns = state_->heap.top().deadline_ns;
      if (deadline_ns > now_ns) {
        state_->condition.wait_for(
            lock, std::chrono::nanoseconds(deadline_ns - now_ns));
        continue;
      }

      std::vector<ReadyTimer> ready;
      CollectReadyLocked(now_ns, &ready);
      lock.unlock();
      for (const ReadyTimer& timer : ready) {
        PostReady(timer);
      }
      lock.lock();
    }
  }

  void DropStaleHeapItemsLocked() {
    while (!state_->heap.empty()) {
      const HeapItem item = state_->heap.top();
      TimerEntry* entry = FindEntryLocked(state_.get(), item.id);
      if (entry != nullptr && entry->active &&
          entry->deadline_ns == item.deadline_ns) {
        break;
      }
      state_->heap.pop();
    }
  }

  bool HasRunnableWorkLocked() const {
    for (const TimerEntry& entry : state_->entries) {
      if (entry.allocated && (entry.active || entry.running != 0)) {
        return true;
      }
    }
    return false;
  }

  void CollectReadyLocked(uint64_t now_ns, std::vector<ReadyTimer>* ready) {
    while (!state_->heap.empty()) {
      const HeapItem item = state_->heap.top();
      if (item.deadline_ns > now_ns) {
        break;
      }
      state_->heap.pop();

      TimerEntry* entry = FindEntryLocked(state_.get(), item.id);
      if (entry == nullptr || !entry->active ||
          entry->deadline_ns != item.deadline_ns) {
        continue;
      }
      if (entry->running >= entry->options.max_concurrency) {
        ++state_->stats.skipped;
        RescheduleSkippedLocked(now_ns, entry);
        continue;
      }

      ++entry->running;
      ReadyTimer timer;
      timer.id = entry->id;
      timer.started_ns = now_ns;
      timer.timeout_ms = entry->options.timeout_ms;
      timer.holder = entry->holder;
      ready->push_back(timer);

      if (!entry->periodic) {
        entry->active = false;
      } else if (entry->options.mode != TimerMode::kDelayUntilComplete) {
        ReschedulePeriodicLocked(now_ns, entry);
      }
    }
  }

  void RescheduleSkippedLocked(uint64_t now_ns, TimerEntry* entry) {
    if (!entry->periodic) {
      entry->active = false;
      FreeEntryLocked(state_.get(), entry);
      return;
    }
    if (entry->options.mode == TimerMode::kDelayUntilComplete) {
      return;
    }
    ReschedulePeriodicLocked(now_ns, entry);
  }

  void ReschedulePeriodicLocked(uint64_t now_ns, TimerEntry* entry) {
    uint64_t next_deadline_ns = now_ns + entry->interval_ns;
    if (entry->options.mode == TimerMode::kFixedRate) {
      next_deadline_ns = entry->deadline_ns + entry->interval_ns;
      while (next_deadline_ns <= now_ns) {
        next_deadline_ns += entry->interval_ns;
      }
    }
    PushTimerLocked(state_.get(), entry, next_deadline_ns);
  }

  void PostReady(const ReadyTimer& timer) {
    if (executor_ == nullptr || !timer.holder || !timer.holder->task) {
      Complete(timer, false);
      return;
    }

    std::shared_ptr<ShardState> state = state_;
    Status error = executor_->Post([state, timer]() mutable {
      timer.holder->task();
      TimerShard::CompleteCallback(state, timer.id, timer.started_ns,
                                   timer.timeout_ms, true);
    });
    if (error != Status::kOk) {
      Complete(timer, false);
      return;
    }

    std::lock_guard<std::mutex> lock(state_->mutex);
    ++state_->stats.fired;
  }

  void Complete(const ReadyTimer& timer, bool ran) {
    CompleteCallback(state_, timer.id, timer.started_ns, timer.timeout_ms, ran);
  }

  static void CompleteCallback(const std::shared_ptr<ShardState>& state,
                               TimerId timer_id, uint64_t started_ns,
                               uint32_t timeout_ms, bool ran) {
    std::lock_guard<std::mutex> lock(state->mutex);
    TimerEntry* entry = FindEntryLocked(state.get(), timer_id);
    if (entry == nullptr || entry->running == 0) {
      return;
    }

    --entry->running;
    if (!ran) {
      ++state->stats.post_failed;
    } else if (timeout_ms > 0 && NowNs() - started_ns > MsToNs(timeout_ms)) {
      ++state->stats.timed_out;
    }

    if (entry->active && entry->periodic &&
        entry->options.mode == TimerMode::kDelayUntilComplete &&
        entry->running == 0) {
      PushTimerLocked(state.get(), entry, NowNs() + entry->interval_ns);
    }

    FreeEntryLocked(state.get(), entry);
    state->stats.active = ActiveCountLocked(*state);
    state->condition.notify_all();
  }

  void DiscardLocked() {
    while (!state_->heap.empty()) {
      state_->heap.pop();
    }
    state_->free_indices.clear();
    for (uint32_t i = 0; i < state_->entries.size(); ++i) {
      TimerEntry& entry = state_->entries[i];
      entry.active = false;
      if (entry.running == 0) {
        entry.id = 0;
        entry.deadline_ns = 0;
        entry.interval_ns = 0;
        entry.holder.reset();
        entry.allocated = false;
        entry.periodic = false;
        state_->free_indices.push_back(i);
      }
    }
    state_->stats.active = ActiveCountLocked(*state_);
  }

  uint32_t index_ = 0;
  Executor* executor_ = nullptr;
  uint32_t max_timers_ = 0;
  std::shared_ptr<ShardState> state_;
  std::thread thread_;
};

}  // namespace

class Timer::Impl {
 public:
  Status Start(const TimerConfig& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
      return Status::kOk;
    }

    options_ = options;
    if (options_.shard_count == 0) {
      options_.shard_count = AutoShardCount();
    }
    if (options_.shard_count == 0 ||
        options_.shard_count > kMaxShardCount ||
        options_.max_timers == 0 ||
        options_.max_timers > kIndexMask ||
        options_.max_pending_callbacks == 0) {
      return Status::kInvalidParam;
    }
    options_.shard_count = std::min(options_.shard_count, options_.max_timers);

    if (options_.callback_executor == nullptr) {
      ExecutorOptions executor_options;
      executor_options.worker_count = options_.callback_worker_count;
      executor_options.queue_capacity = options_.max_pending_callbacks;
      owned_executor_.reset(new Executor());
      const Status error = owned_executor_->Start(executor_options);
      if (error != Status::kOk) {
        owned_executor_.reset();
        return error;
      }
      callback_executor_ = owned_executor_.get();
    } else {
      callback_executor_ = options_.callback_executor;
    }

    shards_.clear();
    shards_.reserve(options_.shard_count);
    const uint32_t base_per_shard =
        options_.max_timers / options_.shard_count;
    const uint32_t extra = options_.max_timers % options_.shard_count;
    for (uint32_t i = 0; i < options_.shard_count; ++i) {
      const uint32_t max_per_shard = base_per_shard + (i < extra ? 1 : 0);
      std::unique_ptr<TimerShard> shard(
          new TimerShard(i, callback_executor_, max_per_shard));
      const Status error = shard->Start();
      if (error != Status::kOk) {
        StopStartedShards();
        return error;
      }
      shards_.push_back(std::move(shard));
    }

    next_shard_.store(0);
    running_ = true;
    return Status::kOk;
  }

  void Stop(StopMode mode) {
    std::vector<std::unique_ptr<TimerShard>> shards;
    std::unique_ptr<Executor> owned_executor;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_ && shards_.empty()) {
        return;
      }
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

  Result<TimerId> Every(uint32_t interval_ms, const TimerOptions& options,
                        Task task) {
    return Add(interval_ms, true, options, std::move(task));
  }

  Status Cancel(TimerId timer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t shard = ShardFromId(timer_id);
    if (!running_ || shard >= shards_.size()) {
      return Status::kNotFound;
    }
    return shards_[shard]->Cancel(timer_id);
  }

  TimerStats GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    TimerStats stats;
    for (const auto& shard : shards_) {
      const TimerStats shard_stats = shard->GetStats();
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
    const uint32_t shard_count = static_cast<uint32_t>(shards_.size());
    const uint32_t first_shard = next_shard_.fetch_add(1) % shard_count;
    Result<TimerId> last_error = Result<TimerId>::Fail(Status::kBusy);
    for (uint32_t i = 0; i < shard_count; ++i) {
      const uint32_t shard = (first_shard + i) % shard_count;
      last_error = shards_[shard]->Add(delay_ms, periodic, options,
                                       std::move(task));
      if (last_error.IsOk() || last_error.status != Status::kBusy) {
        return last_error;
      }
    }
    return last_error;
  }

  void StopStartedShards() {
    for (auto& shard : shards_) {
      shard->Stop(StopMode::kDiscard);
    }
    shards_.clear();
    if (owned_executor_) {
      owned_executor_->Stop(StopMode::kDiscard);
      owned_executor_.reset();
    }
    callback_executor_ = nullptr;
  }

  TimerConfig options_;
  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<TimerShard>> shards_;
  std::unique_ptr<Executor> owned_executor_;
  Executor* callback_executor_ = nullptr;
  std::atomic<uint32_t> next_shard_{0};
  bool running_ = false;
};

Timer::Timer() : impl_(new Impl()) {}

Timer::~Timer() = default;

Status Timer::Start(const TimerConfig& options) {
  return impl_->Start(options);
}

void Timer::Stop(StopMode mode) {
  impl_->Stop(mode);
}

Result<TimerId> Timer::After(uint32_t delay_ms, Task task) {
  return impl_->After(delay_ms, std::move(task));
}

Result<TimerId> Timer::Every(uint32_t interval_ms,
                             const TimerOptions& options,
                             Task task) {
  return impl_->Every(interval_ms, options, std::move(task));
}

Status Timer::Cancel(TimerId timer_id) {
  return impl_->Cancel(timer_id);
}

TimerStats Timer::GetStats() const {
  return impl_->GetStats();
}

}  // namespace infra
