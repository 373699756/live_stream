#include "event.h"

#include "infra/clamp.h"
#include "infra/time.h"

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <unordered_map>

namespace live_stream {
namespace event {
namespace {

constexpr uint32_t kDefaultQueueCapacity = 4096;
constexpr uint32_t kDefaultMaxSubscriptions = 256;
constexpr uint32_t kMaxSourceLength = 64;
constexpr uint32_t kMaxTargetLength = 128;
constexpr uint32_t kMaxMessageLength = 256;

uint32_t AutoWorkerCount() {
    const uint32_t hardware = std::thread::hardware_concurrency();
    if (hardware == 0) {
        return 2;
    }
    return infra::Clamp<uint32_t>(hardware, 1U, 4U);
}

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

bool IsEventSizeValid(const Event &event) {
    return event.source.size() <= kMaxSourceLength &&
           event.target.size() <= kMaxTargetLength &&
           event.message.size() <= kMaxMessageLength;
}

bool ContainsEventType(const std::vector<EventType> &types, EventType type) {
    for (EventType candidate : types) {
        if (candidate == type) {
            return true;
        }
    }
    return false;
}

bool IsEventTypesValid(const std::vector<EventType> &types) {
    if (types.empty()) {
        return false;
    }
    for (EventType type : types) {
        if (!IsKnownEventType(type)) {
            return false;
        }
    }
    return true;
}

Event WithTimestamp(Event event) {
    if (event.timestamp_ms == 0) {
        event.timestamp_ms = infra::Time::SystemTimeMillis();
    }
    return event;
}

}  // namespace

class Executor::Impl {
public:
    bool Start(const ExecutorOptions &options) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return true;
        }

        options_ = options;
        if (options_.worker_count == 0) {
            options_.worker_count = AutoWorkerCount();
        }
        options_.queue_capacity =
            NormalizeQueueCapacity(options_.queue_capacity);
        if (options_.worker_count == 0) {
            return false;
        }

        stopping_ = false;
        discard_ = false;
        running_ = true;
        head_ = 0;
        tail_ = 0;
        size_ = 0;
        running_count_ = 0;
        stats_ = ExecutorStats{};
        tasks_.clear();
        tasks_.resize(options_.queue_capacity);
        workers_.clear();
        workers_.reserve(options_.worker_count);
        for (uint32_t i = 0; i < options_.worker_count; ++i) {
            workers_.push_back(std::thread(&Impl::WorkerLoop, this));
        }
        return true;
    }

    void Stop(StopMode mode) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ && workers_.empty()) {
                return;
            }
            stopping_ = true;
            running_ = false;
            discard_ = mode == StopMode::kDiscard;
            if (discard_) {
                ClearQueueLocked();
            }
        }
        condition_.notify_all();

        for (std::thread &worker : workers_) {
            if (worker.joinable() &&
                worker.get_id() != std::this_thread::get_id()) {
                worker.join();
            }
        }
        workers_.clear();

        std::lock_guard<std::mutex> lock(mutex_);
        ClearQueueLocked();
        running_count_ = 0;
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
            if (size_ >= options_.queue_capacity) {
                ++stats_.rejected;
                return EventStatus::kQueueFull;
            }
            tasks_[tail_] = std::move(task);
            tail_ = (tail_ + 1) % options_.queue_capacity;
            ++size_;
            ++stats_.posted;
            stats_.max_pending = std::max<uint32_t>(
                stats_.max_pending, static_cast<uint32_t>(size_));
        }
        condition_.notify_one();
        return EventStatus::kOk;
    }

    ExecutorStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        ExecutorStats stats = stats_;
        stats.pending = static_cast<uint32_t>(size_);
        stats.running = running_count_;
        stats.worker_count = options_.worker_count;
        return stats;
    }

private:
    void WorkerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ || size_ > 0;
                });
                if ((stopping_ && (discard_ || size_ == 0)) ||
                    (!running_ && size_ == 0)) {
                    break;
                }
                task = std::move(tasks_[head_]);
                tasks_[head_].Reset();
                head_ = (head_ + 1) % options_.queue_capacity;
                --size_;
                ++running_count_;
                ++stats_.wakeups;
            }

            if (task) {
                task();
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                --running_count_;
                ++stats_.completed;
                if (stopping_ && size_ == 0) {
                    condition_.notify_all();
                }
            }
        }
    }

    // Locked suffix means mutex_ is already held by the caller. This helper
    // only rewrites ring-buffer state; it does not join workers or notify.
    void ClearQueueLocked() {
        for (uint32_t i = 0; i < size_; ++i) {
            const uint32_t index = (head_ + i) % options_.queue_capacity;
            tasks_[index].Reset();
        }
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    ExecutorOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<Task> tasks_;
    std::vector<std::thread> workers_;
    ExecutorStats stats_;
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    uint32_t size_ = 0;
    uint32_t running_count_ = 0;
    bool running_ = false;
    bool stopping_ = false;
    bool discard_ = false;
};

Executor::Executor() : impl_(new Impl()) {}
Executor::~Executor() { Stop(StopMode::kDiscard); }

bool Executor::Start(const ExecutorOptions &options) {
    return impl_->Start(options);
}

void Executor::Stop(StopMode mode) { impl_->Stop(mode); }

EventStatus Executor::Post(Task task) { return impl_->Post(std::move(task)); }

ExecutorStats Executor::GetStats() const { return impl_->GetStats(); }

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

    // Locked suffix documents the mutex_ precondition for helpers used inside
    // the condition-variable wait loop.
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

    // Returns -1 when no timer exists; otherwise clamps elapsed deadlines to
    // 1 ms so wait_for() never spins while still waking promptly.
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

class Dispatcher::Impl {
public:
    Subscription SubscribeTypes(Dispatcher *owner,
                                const std::vector<EventType> &types,
                                EventFn fn) {
        if (!owner || !fn || !IsEventTypesValid(types)) {
            return Subscription();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (subscriptions_.size() >= kDefaultMaxSubscriptions) {
            ++counts_.rejected;
            return Subscription();
        }
        const SubscriptionId id = next_subscription_id_++;
        subscriptions_[id] = Entry{types, std::move(fn)};
        return Subscription(owner, id);
    }

    bool Cancel(SubscriptionId id) {
        if (id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return subscriptions_.erase(id) != 0;
    }

    EventStatus Publish(const Event &event) {
        if (!IsKnownEventType(event.type) || !IsEventSizeValid(event)) {
            IncrementRejected();
            return EventStatus::kInvalid;
        }
        const Event event_to_publish = WithTimestamp(event);
        std::vector<EventFn> fns;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto &entry : subscriptions_) {
                if (ContainsEventType(entry.second.types,
                                      event_to_publish.type)) {
                    fns.push_back(entry.second.fn);
                }
            }
            ++counts_.published;
        }
        for (const EventFn &fn : fns) {
            if (fn) {
                fn(event_to_publish);
                std::lock_guard<std::mutex> lock(mutex_);
                ++counts_.handled;
            }
        }
        return EventStatus::kOk;
    }

    EventStatus Post(Dispatcher *owner, Loop *loop, const Event &event) {
        if (owner == nullptr || loop == nullptr) {
            IncrementRejected();
            return EventStatus::kInvalid;
        }
        Event event_to_publish = WithTimestamp(event);
        return loop->Post([owner, event_to_publish]() {
            (void)owner->Publish(event_to_publish);
        });
    }

    EventCounts GetCounts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        EventCounts counts = counts_;
        counts.subscriptions = static_cast<uint32_t>(subscriptions_.size());
        return counts;
    }

private:
    struct Entry {
        std::vector<EventType> types;
        EventFn fn;
    };

    void IncrementRejected() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counts_.rejected;
    }

    mutable std::mutex mutex_;
    std::unordered_map<SubscriptionId, Entry> subscriptions_;
    EventCounts counts_;
    SubscriptionId next_subscription_id_ = 1;
};

Dispatcher::Dispatcher() : impl_(new Impl()) {}
Dispatcher::~Dispatcher() = default;

Subscription Dispatcher::Subscribe(EventType type, EventFn fn) {
    return impl_->SubscribeTypes(this, std::vector<EventType>{type},
                                 std::move(fn));
}

Subscription Dispatcher::SubscribeTypes(const std::vector<EventType> &types,
                                        EventFn fn) {
    return impl_->SubscribeTypes(this, types, std::move(fn));
}

bool Dispatcher::Cancel(SubscriptionId id) { return impl_->Cancel(id); }

EventStatus Dispatcher::Publish(const Event &event) {
    return impl_->Publish(event);
}

EventStatus Dispatcher::Post(Loop *loop, const Event &event) {
    return impl_->Post(this, loop, event);
}

EventCounts Dispatcher::GetCounts() const { return impl_->GetCounts(); }

Subscription::~Subscription() { Cancel(); }

Subscription::Subscription(Subscription &&other) noexcept
    : dispatcher_(other.dispatcher_), id_(other.id_) {
    other.dispatcher_ = nullptr;
    other.id_ = 0;
}

Subscription &Subscription::operator=(Subscription &&other) noexcept {
    if (this != &other) {
        Cancel();
        dispatcher_ = other.dispatcher_;
        id_ = other.id_;
        other.dispatcher_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

void Subscription::Cancel() {
    Dispatcher *dispatcher = dispatcher_;
    const SubscriptionId id = id_;
    dispatcher_ = nullptr;
    id_ = 0;
    if (dispatcher != nullptr && id != 0) {
        (void)dispatcher->Cancel(id);
    }
}

Service::Service() = default;
Service::~Service() { Stop(StopMode::kDiscard); }

bool Service::Start(const ServiceOptions &options) {
    if (started_) {
        return true;
    }
    if (!loop_.Start(options.loop)) {
        return false;
    }
    started_ = true;
    return true;
}

void Service::Stop(StopMode mode) {
    loop_.Stop(mode);
    started_ = false;
}

EventStatus Service::Publish(const Event &event) {
    return dispatcher_.Publish(event);
}

EventStatus Service::PublishAsync(const Event &event) {
    return dispatcher_.Post(&loop_, event);
}

bool IsKnownEventType(EventType type) {
    switch (type) {
        case EventType::kConfigChanged:
        case EventType::kMediaPipelineStarted:
        case EventType::kMediaPipelineStopped:
        case EventType::kMediaPipelineError:
        case EventType::kMediaStatusChanged:
        case EventType::kStreamStarted:
        case EventType::kStreamStopped:
        case EventType::kRtspClientConnected:
        case EventType::kRtspClientDisconnected:
        case EventType::kWebRtcClientConnected:
        case EventType::kWebRtcClientDisconnected:
        case EventType::kOnvifRequestReceived:
        case EventType::kSnapshotCreated:
        case EventType::kTimeChanged:
        case EventType::kNetworkChanged:
        case EventType::kAlarmOn:
        case EventType::kAlarmOff:
        case EventType::kSystemStatusChanged:
        case EventType::kUpgradeProgressChanged:
            return true;
    }
    return false;
}

}  // namespace event
}  // namespace live_stream
