#include "infra/timer.h"

#include "infra/time.h"

#include <atomic>

bool WaitAtLeast(const std::atomic<int>& value, int expected) {
    for (int i = 0; i < 300; ++i) {
        if (value.load() >= expected) {
            return true;
        }
        infra::Time::SleepMillis(1);
    }
    return value.load() >= expected;
}

int main() {
    infra::Executor executor;
    infra::ExecutorOptions executor_options;
    executor_options.worker_count = 2;
    executor_options.queue_capacity = 32;
    if (executor.Start(executor_options) != infra::Status::kOk) {
        return 1;
    }

    infra::Timer timer;
    infra::TimerConfig timer_options;
    timer_options.shard_count = 2;
    timer_options.max_timers = 64;
    timer_options.callback_executor = &executor;
    if (timer.Start(timer_options) != infra::Status::kOk) {
        executor.Stop(infra::StopMode::kDiscard);
        return 2;
    }

    std::atomic<int> once_value{0};
    if (!timer.After(1, [&once_value]() { once_value.fetch_add(1); }).IsOk()) {
        return 3;
    }
    if (!WaitAtLeast(once_value, 1)) {
        return 4;
    }

    std::atomic<int> periodic_value{0};
    infra::TimerOptions options;
    options.mode = infra::TimerMode::kSkipIfRunning;
    infra::Result<infra::TimerId> periodic =
        timer.Every(5, options, [&periodic_value]() {
            periodic_value.fetch_add(1);
        });
    if (!periodic.IsOk()) {
        return 5;
    }
    if (!WaitAtLeast(periodic_value, 2)) {
        return 6;
    }
    if (timer.Cancel(periodic.value) != infra::Status::kOk) {
        return 7;
    }
    if (timer.Cancel(periodic.value) != infra::Status::kNotFound) {
        return 8;
    }

    infra::TimerStats stats = timer.GetStats();
    if (stats.created < 2 || stats.fired < 2 || stats.canceled == 0) {
        return 9;
    }

    timer.Stop(infra::StopMode::kDiscard);
    executor.Stop(infra::StopMode::kDiscard);
    return 0;
}
