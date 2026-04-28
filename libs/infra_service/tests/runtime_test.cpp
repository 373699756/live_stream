#include "infra/runtime.h"

#include "infra/time.h"

#include <atomic>

int main() {
    infra::RuntimeOptions options;
    options.executor.worker_count = 2;
    options.executor.queue_capacity = 32;
    options.timer.shard_count = 2;
    options.timer.max_timers = 64;
    if (infra::Runtime::Init(options) != infra::Status::kOk) {
        return 1;
    }

    std::atomic<int> value{0};
    if (infra::Runtime::Get().Post([&value]() { value.fetch_add(1); }) !=
        infra::Status::kOk) {
        return 2;
    }
    if (!infra::Runtime::Get()
             .After(1, [&value]() { value.fetch_add(1); })
             .IsOk()) {
        return 3;
    }
    for (int i = 0; i < 200 && value.load() < 2; ++i) {
        infra::Time::SleepMillis(1);
    }
    if (value.load() < 2) {
        return 4;
    }
    infra::Runtime::Shutdown();
    return 0;
}
