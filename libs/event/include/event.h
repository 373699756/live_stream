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

// Move-only callback wrapper used by event queues. Small no-throw-movable
// callables live inside the object; larger callables use heap memory so moving
// queued tasks stays noexcept.
template <>
class Fn<void()> {
public:
    Fn() = default;
    Fn(std::nullptr_t) {}

    template <
        typename Callable,
        typename CallableType = typename std::decay<Callable>::type,
        typename = typename std::enable_if<
            !std::is_same<CallableType, Fn>::value>::type>
    Fn(Callable &&callable) {
        Init(std::forward<Callable>(callable));
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
            invoke_(CallablePtr());
        }
    }

    void Reset() {
        if (destroy_ != nullptr) {
            destroy_(CallablePtr());
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

    template <typename CallableType, typename CallableArg>
    void InitCallable(CallableArg &&callable, std::true_type) {
        new (InlinePtr()) CallableType(std::forward<CallableArg>(callable));
        destroy_ = [](void *ptr) {
            static_cast<CallableType *>(ptr)->~CallableType();
        };
        inline_active_ = true;
    }

    template <typename CallableType, typename CallableArg>
    void InitCallable(CallableArg &&callable, std::false_type) {
        CallableType *allocated =
            new (std::nothrow) CallableType(
                std::forward<CallableArg>(callable));
        if (allocated == nullptr) {
            invoke_ = nullptr;
            move_ = nullptr;
            destroy_ = nullptr;
            return;
        }
        destroy_ = [](void *ptr) { delete static_cast<CallableType *>(ptr); };
        heap_ = allocated;
        heap_active_ = true;
    }

    template <typename CallableArg>
    void Init(CallableArg &&callable) {
        using CallableType = typename std::decay<CallableArg>::type;
        invoke_ = [](void *ptr) { (*static_cast<CallableType *>(ptr))(); };
        move_ = [](void *src, void *dst) {
            new (dst) CallableType(
                std::move(*static_cast<CallableType *>(src)));
        };
        InitCallable<CallableType>(
            std::forward<CallableArg>(callable),
            std::integral_constant<
                bool,
                sizeof(CallableType) <= kInlineSize &&
                    alignof(CallableType) <= kInlineAlign &&
                    std::is_nothrow_move_constructible<CallableType>::value>());
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
            move_(other.CallablePtr(), InlinePtr());
            inline_active_ = true;
            other.Reset();
        }
        other.invoke_ = nullptr;
        other.move_ = nullptr;
        other.destroy_ = nullptr;
        other.inline_active_ = false;
    }

    void *InlinePtr() {
        return static_cast<void *>(&inline_bytes_[0]);
    }

    void *CallablePtr() {
        return heap_active_ ? heap_ : InlinePtr();
    }

    alignas(kInlineAlign) unsigned char inline_bytes_[kInlineSize];
    void *heap_ = nullptr;
    void (*invoke_)(void *) = nullptr;
    void (*move_)(void *, void *) = nullptr;
    void (*destroy_)(void *) = nullptr;
    bool inline_active_ = false;
    bool heap_active_ = false;
};

// Task is the work unit accepted by Executor and Loop. Move-only tasks can
// carry ownership without forcing std::function copies.
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

// Bounded worker pool for low-frequency background work. Post() returns
// kQueueFull instead of blocking when callers exceed the configured capacity.
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

// Single-thread execution domain for protocol and service callbacks. Posted
// tasks and timer callbacks run serially on the loop thread.
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

// RAII handle returned by Dispatcher subscriptions. Destroying or cancelling it
// detaches the handler from future Publish() calls.
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

// Synchronous in-process event fanout. Handlers run in the Publish() caller, so
// slow work should be posted to the owning module's queue.
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

// Owns the event loop plus dispatcher used for asynchronous event publishing.
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
