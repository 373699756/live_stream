#ifndef LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_PLATFORM_DEPENDENCIES_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_PLATFORM_DEPENDENCIES_H_

#include <memory>
#include <string>

#include "system/network.h"
#include "system.h"
#include "system/time.h"
#include "system/upgrade.h"

namespace live_stream {

struct DevicePlatformDependencies {
    std::unique_ptr<ISystemPlatform> system_platform;
    std::unique_ptr<ITimePlatform> time_platform;
    std::unique_ptr<INetPlatform> network_platform;
    std::unique_ptr<IUpgradePlatform> upgrade_platform;
    std::string network_ifname;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_PLATFORM_DEPENDENCIES_H_
