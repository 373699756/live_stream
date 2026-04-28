/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: sync.h
 * Brief: Defines infra synchronization primitives.
 */

#ifndef LIVE_STREAM_INFRA_SYNC_H_
#define LIVE_STREAM_INFRA_SYNC_H_

#include "infra/status.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace infra {

class Mutex {
 public:
    Mutex() = default;
    ~Mutex() = default;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() { mutex_.lock(); }
    void Unlock() { mutex_.unlock(); }

 private:
    friend class ConditionVariable;

    std::mutex mutex_;
};

class MutexGuard {
 public:
    explicit MutexGuard(Mutex* mutex) : mutex_(mutex) {
        if (mutex_ != nullptr) {
            mutex_->Lock();
        }
    }

    ~MutexGuard() {
        if (mutex_ != nullptr) {
            mutex_->Unlock();
        }
    }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

 private:
    Mutex* mutex_;
};

class ConditionVariable {
 public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    Status Wait(Mutex* mutex);
    Status WaitFor(Mutex* mutex, uint32_t timeout_ms);
    void NotifyOne();
    void NotifyAll();

 private:
    std::condition_variable condition_variable_;
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_SYNC_H_
