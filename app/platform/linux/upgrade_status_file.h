#ifndef LIVE_STREAM_APP_PLATFORM_LINUX_UPGRADE_STATUS_FILE_H_
#define LIVE_STREAM_APP_PLATFORM_LINUX_UPGRADE_STATUS_FILE_H_

#include "json.h"

#include <string>

namespace live_stream {
namespace linux_platform {

void AppendUpgradeLogLine(const std::string& message);
bool WriteUpgradeStatusFile(const Json& status);

}  // namespace linux_platform
}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_LINUX_UPGRADE_STATUS_FILE_H_
