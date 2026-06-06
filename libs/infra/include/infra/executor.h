/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: executor.h
 * Brief: Defines the infra high-performance async executor.
 */

#ifndef LIVE_STREAM_INFRA_EXECUTOR_H_
#define LIVE_STREAM_INFRA_EXECUTOR_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace infra {

template <typename>
class MoveOnlyFunction;

template <>
class MoveOnlyFunction<void()> {
public:
    MoveOnlyFunction() = default;
    MoveOnlyFunction(std::nullptr_t) {}

    template <
        typename Callable,
        typename Decayed = typename std::decay<Callable>::type,
        typename = typename std::enable_if<
            !std::is_same<Decayed, MoveOnlyFunction>::value>::type>
    MoveOnlyFunction(Callable&& callable) {
        Init<Decayed>(std::forward<Callable>(callable));
    }

    MoveOnlyFunction(MoveOnlyFunction&& other) noexcept {
        MoveFrom(std::move(other));
    }

    MoveOnlyFunction& operator=(MoveOnlyFunction&& other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    ~MoveOnlyFunction() { Reset(); }

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

    template <typename Callable>
    void Init(Callable&& callable) {
        using Stored = typename std::decay<Callable>::type;
        invoke_ = [](void* ptr) { (*static_cast<Stored*>(ptr))(); };
        move_ = [](void* src, void* dst) {
            new (dst) Stored(std::move(*static_cast<Stored*>(src)));
        };

        InitStorage<Stored>(
            std::forward<Callable>(callable),
            std::integral_constant <
                bool,
            sizeof(Stored) <= kInlineSize &&
                alignof(Stored) <= kInlineAlign &&
                std::is_nothrow_move_constructible<Stored>::value > ());
    }

    template <typename Stored, typename Callable>
    void InitStorage(Callable&& callable, std::true_type) {
        new (&storage_) Stored(std::forward<Callable>(callable));
        destroy_ = [](void* ptr) { static_cast<Stored*>(ptr)->~Stored(); };
        inline_active_ = true;
    }

    template <typename Stored, typename Callable>
    void InitStorage(Callable&& callable, std::false_type) {
        Stored* allocated =
            new (std::nothrow) Stored(std::forward<Callable>(callable));
        if (allocated == nullptr) {
            invoke_ = nullptr;
            move_ = nullptr;
            destroy_ = nullptr;
            return;
        }
        destroy_ = [](void* ptr) { delete static_cast<Stored*>(ptr); };
        heap_ = allocated;
        heap_active_ = true;
    }

    void MoveFrom(MoveOnlyFunction&& other) {
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

    void* Ptr() {
        return heap_active_ ? heap_ : static_cast<void*>(&storage_);
    }

    void* Ptr() const {
        return heap_active_
                   ? heap_
                   : const_cast<void*>(static_cast<const void*>(&storage_));
    }

    Storage storage_;
    void* heap_ = nullptr;
    void (*invoke_)(void*) = nullptr;
    void (*move_)(void*, void*) = nullptr;
    void (*destroy_)(void*) = nullptr;
    bool inline_active_ = false;
    bool heap_active_ = false;
};

using Task = MoveOnlyFunction<void()>;

enum class StopMode {
    kDrain,
    kDiscard,
};

struct ExecutorOptions {
    uint32_t worker_count = 0;       // 0 means auto-select.
    uint32_t queue_capacity = 4096;  // Rounded up to at least 1.
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

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    bool Start(const ExecutorOptions& options);
    void Stop(StopMode mode);
    bool Post(Task task);
    ExecutorStats GetStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_EXECUTOR_H_
