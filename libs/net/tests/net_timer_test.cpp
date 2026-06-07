#include "net.h"

#include "infra/time.h"

#include <atomic>

int main() {
    auto engine = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!engine) {
        return 1;
    }
    if (!engine->Start()) {
        return 2;
    }

    std::atomic<bool> fired{false};
    live_stream::NetTimerId timer = engine->RunOnIoAfter(10, [&fired]() {
        fired.store(true);
    });
    if (timer == 0) {
        return 3;
    }
    for (int i = 0; i < 20 && !fired.load(); ++i) {
        infra::Time::SleepMillis(10);
    }
    if (!fired.load()) {
        return 4;
    }
    if (engine->CancelIoTimer(timer)) {
        return 5;
    }

    engine->Stop();
    return 0;
}
