#ifndef LIVE_STREAM_EVENT_EVENT_H_
#define LIVE_STREAM_EVENT_EVENT_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace live_stream {
namespace event {

template <typename>
class Fn;

template <>
class Fn<void()> {
public:
    Fn() = default;
    Fn(std::nullptr_t) {}

    template <
        typename Callable,
        typename Stored = typename std::decay<Callable>::type,
        typename = typename std::enable_if<
            !std::is_same<Stored, Fn>::value>::type>
    Fn(Callable &&callable) {
        Init<Stored>(std::forward<Callable>(callable));
    }

    Fn(Fn &&other) noexcept { MoveFrom(std::move(other)); }

    Fn &operator=(Fn &&other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    Fn(const Fn &) = delete;
    Fn &operator=(const Fn &) = delete;

    ~Fn() { Reset(); }

    explicit operator bool() const { return invoke_ != nullptr; }

    void operator()() {
        if (invoke_ != nullptr) {
            invoke_(Ptr());
        }
    }

    void Reset() {
        if (destroy_ != nullptr) {
            destroy_(Ptr());
        }
        invoke_ = nullptr;
        move_ = nullptr;
        destroy_ = nullptr;
        heap_ = nullptr;
        inline_active_ = false;
        heap_active_ = false;
    }

private:
    static constexpr std::size_t kInlineSize = 64;
    static constexpr std::size_t kInlineAlign = alignof(std::max_align_t);
    using Storage =
        typename std::aligned_storage<kInlineSize, kInlineAlign>::type;

    template <typename Stored, typename Callable>
    void InitStorage(Callable &&callable, std::true_type) {
        new (&storage_) Stored(std::forward<Callable>(callable));
        destroy_ = [](void *ptr) { static_cast<Stored *>(ptr)->~Stored(); };
        inline_active_ = true;
    }

    template <typename Stored, typename Callable>
    void InitStorage(Callable &&callable, std::false_type) {
        Stored *allocated =
            new (std::nothrow) Stored(std::forward<Callable>(callable));
        if (allocated == nullptr) {
            invoke_ = nullptr;
            move_ = nullptr;
            destroy_ = nullptr;
            return;
        }
        destroy_ = [](void *ptr) { delete static_cast<Stored *>(ptr); };
        heap_ = allocated;
        heap_active_ = true;
    }

    template <typename Stored>
    void Init(Stored &&callable) {
        using Saved = typename std::decay<Stored>::type;
        invoke_ = [](void *ptr) { (*static_cast<Saved *>(ptr))(); };
        move_ = [](void *src, void *dst) {
            new (dst) Saved(std::move(*static_cast<Saved *>(src)));
        };
        InitStorage<Saved>(
            std::forward<Stored>(callable),
            std::integral_constant<
                bool,
                sizeof(Saved) <= kInlineSize &&
                    alignof(Saved) <= kInlineAlign &&
                    std::is_nothrow_move_constructible<Saved>::value>());
    }

    void MoveFrom(Fn &&other) {
        invoke_ = other.invoke_;
        move_ = other.move_;
        destroy_ = other.destroy_;
        if (other.heap_active_) {
            heap_ = other.heap_;
            heap_active_ = true;
            other.heap_ = nullptr;
            other.heap_active_ = false;
        } else if (other.inline_active_) {
            move_(other.Ptr(), &storage_);
            inline_active_ = true;
            other.Reset();
        }
        other.invoke_ = nullptr;
        other.move_ = nullptr;
        other.destroy_ = nullptr;
        other.inline_active_ = false;
    }

    void *Ptr() {
        return heap_active_ ? heap_ : static_cast<void *>(&storage_);
    }

    void *Ptr() const {
        return heap_active_
                   ? heap_
                   : const_cast<void *>(
                         static_cast<const void *>(&storage_));
    }

    Storage storage_;
    void *heap_ = nullptr;
    void (*invoke_)(void *) = nullptr;
    void (*move_)(void *, void *) = nullptr;
    void (*destroy_)(void *) = nullptr;
    bool inline_active_ = false;
    bool heap_active_ = false;
};

using Task = Fn<void()>;
using TimerId = uint64_t;
using SubscriptionId = uint64_t;

enum class EventStatus {
    kOk = 0,
    kNotStarted,
    kInvalid,
    kQueueFull,
    kNotFound,
    kCancelled,
};

enum class StopMode {
    kDrain,
    kDiscard,
};

struct ExecutorOptions {
    uint32_t worker_count = 0;
    uint32_t queue_capacity = 4096;
};

struct ExecutorStats {
    uint64_t posted = 0;
    uint64_t completed = 0;
    uint64_t rejected = 0;
    uint64_t wakeups = 0;
    uint32_t pending = 0;
    uint32_t running = 0;
    uint32_t max_pending = 0;
    uint32_t worker_count = 0;
};

class Executor {
public:
    Executor();
    ~Executor();

    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;

    bool Start(const ExecutorOptions &options);
    void Stop(StopMode mode);
    EventStatus Post(Task task);
    ExecutorStats GetStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct LoopOptions {
    std::string name;
    uint32_t queue_capacity = 4096;
    bool enable_thread_affinity = false;
    uint32_t cpu = 0;
};

struct LoopStats {
    uint64_t posted = 0;
    uint64_t completed = 0;
    uint64_t rejected = 0;
    uint64_t timer_fired = 0;
    uint64_t timer_cancelled = 0;
    uint32_t pending = 0;
    uint32_t max_pending = 0;
    uint32_t active_timers = 0;
    bool running = false;
};

class Loop {
public:
    Loop();
    virtual ~Loop();

    Loop(const Loop &) = delete;
    Loop &operator=(const Loop &) = delete;

    virtual bool Start(const LoopOptions &options = LoopOptions());
    virtual void Stop(StopMode mode);
    virtual EventStatus Post(Task task);
    virtual EventStatus RunAfter(uint32_t delay_ms, Task task,
                                 TimerId *timer_id);
    virtual EventStatus RunEvery(uint32_t interval_ms, Task task,
                                 TimerId *timer_id);
    virtual bool CancelTimer(TimerId timer_id);
    virtual bool IsCurrentThread() const;
    virtual LoopStats GetStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

enum class EventType {
    kConfigChanged,
    kMediaPipelineStarted,
    kMediaPipelineStopped,
    kMediaPipelineError,
    kMediaStatusChanged,
    kStreamStarted,
    kStreamStopped,
    kRtspClientConnected,
    kRtspClientDisconnected,
    kWebRtcClientConnected,
    kWebRtcClientDisconnected,
    kOnvifRequestReceived,
    kSnapshotCreated,
    kTimeChanged,
    kNetworkChanged,
    kAlarmOn,
    kAlarmOff,
    kSystemStatusChanged,
    kUpgradeProgressChanged,
};

// Event is for lightweight state/control metadata. Media frames, credentials,
// binary payloads and large JSON stay in the module that owns that data.
struct Event {
    EventType type = EventType::kConfigChanged;
    std::string source;
    std::string target;
    std::string message;
    int32_t value = 0;
    int64_t timestamp_ms = 0;
    uint8_t level = 0;
};

struct EventCounts {
    uint64_t published = 0;
    uint64_t handled = 0;
    uint64_t rejected = 0;
    uint32_t subscriptions = 0;
};

using EventFn = std::function<void(const Event &)>;

class Dispatcher;

class Subscription {
public:
    Subscription() = default;
    ~Subscription();

    Subscription(Subscription &&other) noexcept;
    Subscription &operator=(Subscription &&other) noexcept;

    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;

    bool valid() const { return dispatcher_ != nullptr && id_ != 0; }
    void Cancel();

private:
    friend class Dispatcher;

    Subscription(Dispatcher *dispatcher, SubscriptionId id)
        : dispatcher_(dispatcher), id_(id) {}

    Dispatcher *dispatcher_ = nullptr;
    SubscriptionId id_ = 0;
};

class Dispatcher {
public:
    Dispatcher();
    ~Dispatcher();

    Dispatcher(const Dispatcher &) = delete;
    Dispatcher &operator=(const Dispatcher &) = delete;

    Subscription Subscribe(EventType type, EventFn fn);
    Subscription SubscribeMany(const std::vector<EventType> &types,
                               EventFn fn);
    bool Cancel(SubscriptionId id);
    EventStatus Publish(const Event &event);
    EventStatus Post(Loop *loop, const Event &event);
    EventCounts GetCounts() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct ServiceOptions {
    LoopOptions loop;
};

class Service {
public:
    Service();
    ~Service();

    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    bool Start(const ServiceOptions &options = ServiceOptions());
    void Stop(StopMode mode = StopMode::kDiscard);
    Loop *loop() { return &loop_; }
    Dispatcher *dispatcher() { return &dispatcher_; }
    EventStatus Publish(const Event &event);
    EventStatus PublishAsync(const Event &event);

private:
    Loop loop_;
    Dispatcher dispatcher_;
    bool started_ = false;
};

bool IsKnownEventType(EventType type);

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_EVENT_H_
