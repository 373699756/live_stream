#ifndef LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_PROCESS_H_
#define LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_PROCESS_H_

#include <string>
#include <vector>

namespace live_stream {
namespace linux_platform {

int RunCommand(const std::vector<std::string> &argv);
bool RunAny(const std::vector<std::vector<std::string>> &commands);
bool IsExecutable(const std::string &path);

}  // namespace linux_platform
}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_PROCESS_H_
