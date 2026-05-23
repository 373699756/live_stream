#ifndef LIVE_STREAM_APP_DEVICE_PLATFORMS_H_
#define LIVE_STREAM_APP_DEVICE_PLATFORMS_H_

#include <memory>
#include <string>

#include "network_service.h"
#include "system_service.h"
#include "time_service.h"
#include "upgrade_service.h"

namespace live_stream {

std::unique_ptr<ISystemPlatform> CreateLinuxSystemPlatform();
std::unique_ptr<ITimePlatform> CreateLinuxTimePlatform();
std::unique_ptr<INetworkPlatform>
CreateNetworkPlatform(const std::string &default_ifname);
std::unique_ptr<IUpgradePlatform> CreateUpgradePlatform();

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_DEVICE_PLATFORMS_H_
