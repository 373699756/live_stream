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

    if (executor.Post([]() {}) != infra::Status::kBusy) {
        return 1;
    }
    if (executor.Start(options) != infra::Status::kOk) {
        return 2;
    }
    if (executor.Start(options) != infra::Status::kOk) {
        return 3;
    }

    std::atomic<int> completed{0};
    for (int i = 0; i < 4; ++i) {
        if (executor.Post([&completed]() { completed.fetch_add(1); }) !=
            infra::Status::kOk) {
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
    if (executor.Post([]() {}) != infra::Status::kBusy) {
        return 7;
    }
    return 0;
}
