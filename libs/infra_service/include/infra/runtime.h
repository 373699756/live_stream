/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: runtime.h
 * Brief: Defines the process-wide infra async runtime.
 */

#ifndef LIVE_STREAM_INFRA_RUNTIME_H_
#define LIVE_STREAM_INFRA_RUNTIME_H_

#include "infra/executor.h"
#include "infra/status.h"
#include "infra/timer.h"

namespace infra {

struct RuntimeOptions {
    ExecutorOptions executor;
    TimerConfig timer;
};

struct RuntimeStats {
    ExecutorStats executor;
    TimerStats timer;
};

class Runtime {
 public:
    static Status Init(const RuntimeOptions& options);
    static Runtime& Get();
    static void Shutdown();

    Status Post(Task task);
    Result<TimerId> After(uint32_t delay_ms, Task task);
    Result<TimerId> Every(uint32_t interval_ms,
                          const TimerOptions& options,
                          Task task);
    Status Cancel(TimerId timer_id);
    RuntimeStats GetStats() const;

 private:
    Runtime() = default;
    ~Runtime() = default;

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Executor executor_;
    Timer timer_;
    bool initialized_ = false;
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_RUNTIME_H_
