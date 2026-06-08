#include "platform/linux/platform_factory.h"

#include "platform/linux/device_platforms.h"

namespace live_stream {

DevicePlatformDependencies CreateLinuxDevicePlatformDependencies(
    const std::string &network_ifname) {
    DevicePlatformDependencies dependencies;
    dependencies.system_platform = CreateSystemPlatform();
    dependencies.time_platform = CreateTimePlatform();
    dependencies.network_platform = CreateNetworkPlatform(network_ifname);
    dependencies.upgrade_platform = CreateUpgradePlatform();
    dependencies.network_ifname = network_ifname;
    return dependencies;
}

}  // namespace live_stream
