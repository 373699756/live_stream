#include "infra/executor.h"

#include "infra/time.h"

#include <atomic>

bool WaitForValue(const std::atomic<int>& value, int expected) {
    for (int i = 0; i < 200; ++i) {
        if (value.load() == expected) {
            return true;
        }
        infra::Time::SleepMillis(1);
    }
    return value.load() == expected;
}

int main() {
    infra::Executor executor;
    infra::ExecutorOptions options;
    options.worker_count = 2;
    options.queue_capacity = 8;

    if (executor.Post([]() {})) {
        return 1;
    }
    if (!executor.Start(options)) {
        return 2;
    }
    if (!executor.Start(options)) {
        return 3;
    }

    std::atomic<int> completed{0};
    for (int i = 0; i < 4; ++i) {
        if (!executor.Post([&completed]() { completed.fetch_add(1); })) {
            return 4;
        }
    }
    if (!WaitForValue(completed, 4)) {
        return 5;
    }

    infra::ExecutorStats stats = executor.GetStats();
    if (stats.posted < 4 || stats.completed < 4 || stats.worker_count != 2) {
        return 6;
    }

    executor.Stop(infra::StopMode::kDrain);
    if (executor.Post([]() {})) {
        return 7;
    }

    if (!executor.Start(options)) {
        return 8;
    }
    stats = executor.GetStats();
    if (stats.posted != 0 || stats.completed != 0 || stats.rejected != 0) {
        return 9;
    }
    executor.Stop(infra::StopMode::kDiscard);

    options.worker_count = 1;
    options.queue_capacity = 1;
    if (!executor.Start(options)) {
        return 10;
    }
    std::atomic<int> blocked{0};
    if (executor.Post([&blocked]() {
            infra::Time::SleepMillis(20);
            blocked.fetch_add(1);
        })) {
        return 11;
    }
    if (!executor.Post([&blocked]() { blocked.fetch_add(1); })) {
        return 12;
    }
    if (executor.Post([]() {})) {
        return 13;
    }
    executor.Stop(infra::StopMode::kDrain);
    if (!WaitForValue(blocked, 2)) {
        return 14;
    }

    options.worker_count = 1;
    options.queue_capacity = 4;
    if (!executor.Start(options)) {
        return 15;
    }
    std::atomic<int> discard_value{0};
    for (int i = 0; i < 4; ++i) {
        static_cast<void>(executor.Post([&discard_value]() {
            infra::Time::SleepMillis(20);
            discard_value.fetch_add(1);
        }));
    }
    executor.Stop(infra::StopMode::kDiscard);
    if (discard_value.load() >= 4) {
        return 16;
    }
    return 0;
}
