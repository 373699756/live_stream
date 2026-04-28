#include "netframe_service.h"

#include "infra/time.h"

#include <atomic>

int main() {
    auto engine = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!engine.IsOk()) {
        return 1;
    }
    if (engine.value->Start() != infra::Status::kOk) {
        return 2;
    }

    std::atomic<bool> fired{false};
    auto timer = engine.value->RunOnIoAfter(10, [&fired]() {
        fired.store(true);
    });
    if (!timer.IsOk()) {
        return 3;
    }
    for (int i = 0; i < 20 && !fired.load(); ++i) {
        infra::Time::SleepMillis(10);
    }
    if (!fired.load()) {
        return 4;
    }
    if (engine.value->CancelIoTimer(timer.value) != infra::Status::kNotFound) {
        return 5;
    }

    engine.value->Stop();
    return 0;
}
