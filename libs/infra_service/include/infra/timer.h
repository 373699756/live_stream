/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: timer.h
 * Brief: Defines the infra asynchronous multi-shard timer service.
 */

#ifndef LIVE_STREAM_INFRA_TIMER_H_
#define LIVE_STREAM_INFRA_TIMER_H_

#include "infra/executor.h"
#include "infra/status.h"

#include <cstdint>
#include <memory>

namespace infra {

using TimerId = uint64_t;

enum class TimerMode {
    // Skip a periodic firing when the previous callback is still running.
    kSkipIfRunning,
    // Allow overlapping callbacks up to max_concurrency.
    kAllowOverlap,
    // Start the next interval after the previous callback completes.
    kDelayUntilComplete,
    // Advance by the configured interval and do not replay missed firings.
    kFixedRate,
};

struct TimerOptions {
    TimerMode mode = TimerMode::kSkipIfRunning;
    uint32_t max_concurrency = 1;
    // 0 disables timeout accounting; nonzero values update TimerStats only.
    uint32_t timeout_ms = 0;
};

struct TimerConfig {
    uint32_t shard_count = 0;
    uint32_t max_timers = 65536;
    Executor* callback_executor = nullptr;
    uint32_t callback_worker_count = 0;
    uint32_t max_pending_callbacks = 4096;
};

struct TimerStats {
    uint64_t created = 0;
    uint64_t fired = 0;
    uint64_t canceled = 0;
    uint64_t skipped = 0;
    uint64_t timed_out = 0;
    uint64_t post_failed = 0;
    uint32_t active = 0;
};

class Timer {
 public:
    Timer();
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    Status Start(const TimerConfig& options);
    void Stop(StopMode mode);

    Result<TimerId> After(uint32_t delay_ms, Task task);
    Result<TimerId> Every(uint32_t interval_ms,
                          const TimerOptions& options,
                          Task task);
    Status Cancel(TimerId timer_id);

    TimerStats GetStats() const;

 private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_TIMER_H_
