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
    live_stream::INetExecutor *executor = engine->DefaultExecutor();
    if (executor == nullptr) {
        return 3;
    }

    std::atomic<bool> fired{false};
    live_stream::NetTimerId timer = executor->RunAfter(10, [&fired]() {
        fired.store(true);
    });
    if (timer == 0) {
        return 4;
    }
    for (int i = 0; i < 20 && !fired.load(); ++i) {
        infra::Time::SleepMillis(10);
    }
    if (!fired.load()) {
        return 5;
    }
    if (executor->CancelTimer(timer)) {
        return 6;
    }

    engine->Stop();
    return 0;
}
