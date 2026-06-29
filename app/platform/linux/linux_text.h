#ifndef LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_TEXT_H_
#define LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_TEXT_H_

#include <string>
#include <vector>

namespace live_stream {
namespace linux_platform {

std::string Trim(const std::string &value);
std::string ReadFirstText(const std::vector<std::string> &paths);

}  // namespace linux_platform
}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_LINUX_LINUX_TEXT_H_
