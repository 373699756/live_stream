#ifndef LIVE_STREAM_APP_LINUX_PLATFORM_COMMON_H_
#define LIVE_STREAM_APP_LINUX_PLATFORM_COMMON_H_

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace linux_platform {

std::string Trim(const std::string &value);
std::string ReadFirstText(const std::vector<std::string> &paths);
int64_t ReadSystemTimeMs();
bool SetSystemTimeMsInternal(int64_t unix_time_ms);
int RunCommand(const std::vector<std::string> &argv);
bool RunAny(const std::vector<std::vector<std::string>> &commands);
bool IsExecutable(const std::string &path);

}  // namespace linux_platform
}  // namespace live_stream

#endif  // LIVE_STREAM_APP_LINUX_PLATFORM_COMMON_H_
