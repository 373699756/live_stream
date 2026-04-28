#include "infra/runtime.h"

#include <mutex>
#include <utility>

namespace infra {
namespace {

std::mutex g_runtime_mutex;

}  // namespace

Runtime& Runtime::Get() {
    static Runtime runtime;
    return runtime;
}

Status Runtime::Init(const RuntimeOptions& options) {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    Runtime& runtime = Get();
    if (runtime.initialized_) {
        return Status::kOk;
    }

    const Status executor_error = runtime.executor_.Start(options.executor);
    if (executor_error != Status::kOk) {
        return executor_error;
    }

    TimerConfig timer_options = options.timer;
    timer_options.callback_executor = &runtime.executor_;
    const Status timer_error = runtime.timer_.Start(timer_options);
    if (timer_error != Status::kOk) {
        runtime.executor_.Stop(StopMode::kDiscard);
        return timer_error;
    }

    runtime.initialized_ = true;
    return Status::kOk;
}

void Runtime::Shutdown() {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    Runtime& runtime = Get();
    if (!runtime.initialized_) {
        return;
    }
    runtime.timer_.Stop(StopMode::kDiscard);
    runtime.executor_.Stop(StopMode::kDiscard);
    runtime.initialized_ = false;
}

Status Runtime::Post(Task task) {
    return executor_.Post(std::move(task));
}

Result<TimerId> Runtime::After(uint32_t delay_ms, Task task) {
    return timer_.After(delay_ms, std::move(task));
}

Result<TimerId> Runtime::Every(uint32_t interval_ms,
                               const TimerOptions& options,
                               Task task) {
    return timer_.Every(interval_ms, options, std::move(task));
}

Status Runtime::Cancel(TimerId timer_id) {
    return timer_.Cancel(timer_id);
}

RuntimeStats Runtime::GetStats() const {
    RuntimeStats stats;
    stats.executor = executor_.GetStats();
    stats.timer = timer_.GetStats();
    return stats;
}

}  // namespace infra
