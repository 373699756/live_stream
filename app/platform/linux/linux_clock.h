#ifndef LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_CLOCK_H_
#define LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_CLOCK_H_

#include <cstdint>

namespace live_stream {
namespace linux_platform {

int64_t ReadSystemTimeMs();
bool SetSystemTimeMsInternal(int64_t unix_time_ms);

}  // namespace linux_platform
}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_CLOCK_H_
