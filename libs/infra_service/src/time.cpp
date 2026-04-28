#include "infra/time.h"

#include <chrono>
#include <ctime>
#include <thread>

namespace infra {

int64_t Time::MonotonicMillis() {
    return MonotonicNanos() / 1000000LL;
}

int64_t Time::MonotonicNanos() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int64_t Time::SystemTimeMillis() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void Time::SleepMillis(uint32_t millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

}  // namespace infra
