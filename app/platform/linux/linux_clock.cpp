#include "platform/linux/linux_clock.h"

#include <ctime>

namespace live_stream {
namespace linux_platform {

int64_t ReadSystemTimeMs() {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return 0;
    }
    return static_cast<int64_t>(now.tv_sec) * 1000LL +
           static_cast<int64_t>(now.tv_nsec / 1000000LL);
}

bool SetSystemTimeMsInternal(int64_t unix_time_ms) {
    if (unix_time_ms <= 0) {
        return false;
    }
    struct timespec value;
    value.tv_sec = static_cast<time_t>(unix_time_ms / 1000LL);
    value.tv_nsec = static_cast<long>((unix_time_ms % 1000LL) * 1000000LL);
    return clock_settime(CLOCK_REALTIME, &value) == 0;
}

}  // namespace linux_platform
}  // namespace live_stream
