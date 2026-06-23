#ifndef LIVE_STREAM_EVENT_EXECUTOR_H_
#define LIVE_STREAM_EVENT_EXECUTOR_H_

#include "status.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

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

using Task = Fn<void()>;

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

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_EXECUTOR_H_
