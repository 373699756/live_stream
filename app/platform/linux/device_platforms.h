#ifndef LIVE_STREAM_APP_PLATFORM_LINUX_DEVICE_PLATFORMS_H_
#define LIVE_STREAM_APP_PLATFORM_LINUX_DEVICE_PLATFORMS_H_

#include <memory>
#include <string>

#include "system/network.h"
#include "system.h"
#include "system/time.h"
#include "system/upgrade.h"

namespace live_stream {

std::unique_ptr<ISystemPlatform> CreateSystemPlatform();
std::unique_ptr<ITimePlatform> CreateTimePlatform();
std::unique_ptr<INetPlatform>
CreateNetworkPlatform(const std::string &default_ifname);
std::unique_ptr<IUpgradePlatform> CreateUpgradePlatform();

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_LINUX_DEVICE_PLATFORMS_H_
