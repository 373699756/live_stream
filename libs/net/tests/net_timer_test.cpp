#include "net.h"

#include "infra/time.h"

#include <atomic>

int main() {
    auto engine = live_stream::CreateNetIo(live_stream::NetIoOptions{});
    if (!engine) {
        return 1;
    }
    if (!engine->Start()) {
        return 2;
    }
    live_stream::event::Loop *loop = engine->DefaultLoop();
    if (loop == nullptr) {
        return 3;
    }

    std::atomic<bool> fired{false};
    live_stream::event::TimerId timer = 0;
    if (loop->RunAfter(10, [&fired]() { fired.store(true); }, &timer) !=
        live_stream::event::EventStatus::kOk) {
        return 4;
    }
    if (timer == 0) {
        return 5;
    }
    for (int i = 0; i < 20 && !fired.load(); ++i) {
        infra::Time::SleepMillis(10);
    }
    if (!fired.load()) {
        return 6;
    }
    if (loop->CancelTimer(timer)) {
        return 7;
    }

    engine->Stop();
    return 0;
}
