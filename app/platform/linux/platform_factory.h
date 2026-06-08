#ifndef LIVE_STREAM_APP_PLATFORM_LINUX_PLATFORM_FACTORY_H_
#define LIVE_STREAM_APP_PLATFORM_LINUX_PLATFORM_FACTORY_H_

#include <string>

#include "subsystems/device_platform_dependencies.h"

namespace live_stream {

// Creates Linux-specific platform dependencies in one place.
// network_ifname: the primary network interface (e.g. "eth0"). Must be
// non-empty.
DevicePlatformDependencies CreateLinuxDevicePlatformDependencies(
    const std::string &network_ifname);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_LINUX_PLATFORM_FACTORY_H_
