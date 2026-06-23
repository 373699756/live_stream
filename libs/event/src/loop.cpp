#include "loop.h"

#include "infra/time.h"

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <utility>
#include <vector>

namespace live_stream {
namespace event {
namespace {

constexpr uint32_t kDefaultQueueCapacity = 4096;

uint32_t NormalizeQueueCapacity(uint32_t queue_capacity) {
    return queue_capacity == 0 ? kDefaultQueueCapacity : queue_capacity;
}

void SetCurrentThreadAffinity(uint32_t cpu) {
    if (cpu >= CPU_SETSIZE) {
        return;
    }
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu, &cpu_set);
    (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
}

}  // namespace

class Loop::Impl {
public:
    ~Impl() { Stop(StopMode::kDiscard); }

    bool Start(const LoopOptions &options) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return true;
        }
        options_ = options;
        options_.queue_capacity =
            NormalizeQueueCapacity(options_.queue_capacity);
        stopping_ = false;
        discard_ = false;
        running_ = true;
        tasks_.clear();
        timers_.clear();
        stats_ = LoopStats{};
        next_timer_id_ = 1;
        thread_ = std::thread(&Impl::Run, this);
        return true;
    }

    void Stop(StopMode mode) {
        bool should_join = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ && !thread_.joinable()) {
                return;
            }
            stopping_ = true;
            running_ = false;
            discard_ = mode == StopMode::kDiscard;
            if (discard_) {
                tasks_.clear();
            }
            timers_.clear();
            should_join =
                thread_.joinable() &&
                thread_.get_id() != std::this_thread::get_id();
        }
        condition_.notify_all();
        if (should_join) {
            thread_.join();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.clear();
        timers_.clear();
        thread_id_ = std::thread::id();
    }

    EventStatus Post(Task task) {
        if (!task) {
            return EventStatus::kInvalid;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || stopping_) {
                ++stats_.rejected;
                return EventStatus::kNotStarted;
            }
            if (tasks_.size() >= options_.queue_capacity) {
                ++stats_.rejected;
                return EventStatus::kQueueFull;
            }
            tasks_.push_back(std::move(task));
            ++stats_.posted;
            stats_.max_pending = std::max<uint32_t>(
                stats_.max_pending, static_cast<uint32_t>(tasks_.size()));
        }
        condition_.notify_one();
        return EventStatus::kOk;
    }

    EventStatus RunAfter(uint32_t delay_ms, Task task, TimerId *timer_id) {
        return AddTimer(delay_ms, 0, std::move(task), timer_id);
    }

    EventStatus RunEvery(uint32_t interval_ms, Task task,
                         TimerId *timer_id) {
        if (interval_ms == 0) {
            return EventStatus::kInvalid;
        }
        return AddTimer(interval_ms, interval_ms, std::move(task), timer_id);
    }

    bool CancelTimer(TimerId timer_id) {
        if (timer_id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = timers_.find(timer_id);
        if (it == timers_.end()) {
            return false;
        }
        it->second.cancelled = true;
        timers_.erase(it);
        ++stats_.timer_cancelled;
        condition_.notify_all();
        return true;
    }

    bool IsCurrentThread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return thread_id_ == std::this_thread::get_id();
    }

    LoopStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        LoopStats stats = stats_;
        stats.pending = static_cast<uint32_t>(tasks_.size());
        stats.active_timers = static_cast<uint32_t>(timers_.size());
        stats.running = running_;
        return stats;
    }

private:
    struct Timer {
        int64_t deadline_ms = 0;
        uint32_t interval_ms = 0;
        Task task;
        bool cancelled = false;
    };

    EventStatus AddTimer(uint32_t delay_ms, uint32_t interval_ms, Task task,
                         TimerId *timer_id) {
        if (timer_id == nullptr || !task) {
            return EventStatus::kInvalid;
        }
        *timer_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || stopping_) {
                ++stats_.rejected;
                return EventStatus::kNotStarted;
            }
            const TimerId next_timer_id = next_timer_id_++;
            Timer timer;
            timer.deadline_ms = infra::Time::MonotonicMillis() + delay_ms;
            timer.interval_ms = interval_ms;
            timer.task = std::move(task);
            timers_[next_timer_id] = std::move(timer);
            *timer_id = next_timer_id;
        }
        condition_.notify_one();
        return EventStatus::kOk;
    }

    bool IsWorkReadyLocked() const {
        return !tasks_.empty() || IsTimerReadyLocked();
    }

    bool IsTimerReadyLocked() const {
        if (timers_.empty()) {
            return false;
        }
        const int64_t now_ms = infra::Time::MonotonicMillis();
        for (const auto &entry : timers_) {
            if (entry.second.deadline_ms <= now_ms) {
                return true;
            }
        }
        return false;
    }

    int64_t NextTimerDelayMsLocked() const {
        if (timers_.empty()) {
            return -1;
        }
        const int64_t now_ms = infra::Time::MonotonicMillis();
        int64_t next_deadline_ms = timers_.begin()->second.deadline_ms;
        for (const auto &entry : timers_) {
            next_deadline_ms =
                std::min<int64_t>(next_deadline_ms,
                                  entry.second.deadline_ms);
        }
        const int64_t delay_ms = next_deadline_ms - now_ms;
        return delay_ms < 1 ? 1 : delay_ms;
    }

    void Run() {
        if (options_.enable_thread_affinity) {
            SetCurrentThreadAffinity(options_.cpu);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            thread_id_ = std::this_thread::get_id();
        }

        while (true) {
            std::vector<Task> tasks;
            std::vector<std::pair<TimerId, Timer>> ready_timers;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                while (!stopping_ && !IsWorkReadyLocked()) {
                    const int64_t delay_ms = NextTimerDelayMsLocked();
                    if (delay_ms < 0) {
                        condition_.wait(lock);
                    } else {
                        condition_.wait_for(
                            lock, std::chrono::milliseconds(delay_ms));
                    }
                }
                if (stopping_ && (discard_ || tasks_.empty()) &&
                    timers_.empty()) {
                    break;
                }
                tasks.swap(tasks_);

                const int64_t now_ms = infra::Time::MonotonicMillis();
                for (auto it = timers_.begin(); it != timers_.end();) {
                    if (it->second.deadline_ms > now_ms) {
                        ++it;
                        continue;
                    }
                    ready_timers.push_back(
                        std::make_pair(it->first, std::move(it->second)));
                    it = timers_.erase(it);
                }
            }

            for (Task &task : tasks) {
                if (task) {
                    task();
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++stats_.completed;
                }
            }

            for (auto &entry : ready_timers) {
                Timer &timer = entry.second;
                if (timer.cancelled || !timer.task) {
                    continue;
                }
                timer.task();
                std::lock_guard<std::mutex> lock(mutex_);
                ++stats_.timer_fired;
                if (!stopping_ && !timer.cancelled &&
                    timer.interval_ms != 0) {
                    timer.deadline_ms =
                        infra::Time::MonotonicMillis() + timer.interval_ms;
                    timers_[entry.first] = std::move(timer);
                }
            }
        }
    }

    LoopOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    std::thread::id thread_id_;
    std::vector<Task> tasks_;
    std::map<TimerId, Timer> timers_;
    LoopStats stats_;
    TimerId next_timer_id_ = 1;
    bool running_ = false;
    bool stopping_ = false;
    bool discard_ = false;
};

Loop::Loop() : impl_(new Impl()) {}
Loop::~Loop() = default;

bool Loop::Start(const LoopOptions &options) { return impl_->Start(options); }

void Loop::Stop(StopMode mode) { impl_->Stop(mode); }

EventStatus Loop::Post(Task task) { return impl_->Post(std::move(task)); }

EventStatus Loop::RunAfter(uint32_t delay_ms, Task task, TimerId *timer_id) {
    return impl_->RunAfter(delay_ms, std::move(task), timer_id);
}

EventStatus Loop::RunEvery(uint32_t interval_ms, Task task,
                           TimerId *timer_id) {
    return impl_->RunEvery(interval_ms, std::move(task), timer_id);
}

bool Loop::CancelTimer(TimerId timer_id) {
    return impl_->CancelTimer(timer_id);
}

bool Loop::IsCurrentThread() const { return impl_->IsCurrentThread(); }

LoopStats Loop::GetStats() const { return impl_->GetStats(); }

}  // namespace event
}  // namespace live_stream
