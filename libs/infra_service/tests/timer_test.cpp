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

    infra::Result<infra::TimerId> canceled =
        timer.After(100, []() {});
    if (!canceled.IsOk()) {
        return 9;
    }
    if (timer.Cancel(canceled.value) != infra::Status::kOk) {
        return 10;
    }
    if (timer.Cancel(canceled.value) != infra::Status::kNotFound) {
        return 11;
    }

    std::atomic<int> skipped_value{0};
    infra::TimerOptions skip_options;
    skip_options.mode = infra::TimerMode::kSkipIfRunning;
    skip_options.max_concurrency = 1;
    infra::Result<infra::TimerId> skipped =
        timer.Every(1, skip_options, [&skipped_value]() {
            infra::Time::SleepMillis(10);
            skipped_value.fetch_add(1);
        });
    if (!skipped.IsOk()) {
        return 12;
    }
    infra::Time::SleepMillis(40);
    if (timer.Cancel(skipped.value) != infra::Status::kOk) {
        return 13;
    }

    std::atomic<int> delay_value{0};
    infra::TimerOptions delay_options;
    delay_options.mode = infra::TimerMode::kDelayUntilComplete;
    infra::Result<infra::TimerId> delayed =
        timer.Every(1, delay_options, [&delay_value]() {
            infra::Time::SleepMillis(5);
            delay_value.fetch_add(1);
        });
    if (!delayed.IsOk()) {
        return 14;
    }
    infra::Time::SleepMillis(25);
    if (timer.Cancel(delayed.value) != infra::Status::kOk) {
        return 15;
    }

    infra::TimerOptions invalid_options;
    invalid_options.max_concurrency = 0;
    if (timer.Every(1, invalid_options, []() {}).status !=
        infra::Status::kInvalidParam) {
        return 16;
    }

    std::atomic<int> timeout_value{0};
    infra::TimerOptions timeout_options;
    timeout_options.timeout_ms = 1;
    infra::Result<infra::TimerId> timeout =
        timer.Every(20, timeout_options, [&timeout_value]() {
            infra::Time::SleepMillis(5);
            timeout_value.fetch_add(1);
        });
    if (!timeout.IsOk()) {
        return 17;
    }
    if (!WaitAtLeast(timeout_value, 1)) {
        return 18;
    }
    infra::Time::SleepMillis(2);
    if (timer.Cancel(timeout.value) != infra::Status::kOk) {
        return 19;
    }

    infra::TimerStats stats = timer.GetStats();
    if (stats.created < 6 || stats.fired < 4 || stats.canceled < 4 ||
        stats.skipped == 0 || stats.timed_out == 0) {
        return 20;
    }

    timer.Stop(infra::StopMode::kDiscard);

    infra::Timer limited;
    infra::TimerConfig limited_options;
    limited_options.shard_count = 1;
    limited_options.max_timers = 2;
    limited_options.callback_executor = &executor;
    if (limited.Start(limited_options) != infra::Status::kOk) {
        executor.Stop(infra::StopMode::kDiscard);
        return 21;
    }
    infra::Result<infra::TimerId> limited_a =
        limited.After(100, []() {});
    infra::Result<infra::TimerId> limited_b =
        limited.After(100, []() {});
    infra::Result<infra::TimerId> limited_c =
        limited.After(100, []() {});
    if (!limited_a.IsOk() || !limited_b.IsOk() ||
        limited_c.status != infra::Status::kBusy) {
        return 22;
    }
    limited.Stop(infra::StopMode::kDiscard);

    executor.Stop(infra::StopMode::kDiscard);
    return 0;
}
