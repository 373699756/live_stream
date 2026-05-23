#include "platform_factory.h"

#include "device_platforms.h"

namespace live_stream {

PlatformAdapters CreateLinuxPlatformAdapters(const std::string& network_ifname) {
    PlatformAdapters adapters;
    adapters.system = CreateLinuxSystemPlatform();
    adapters.time = CreateLinuxTimePlatform();
    adapters.network = CreateNetworkPlatform(network_ifname);
    adapters.upgrade = CreateUpgradePlatform();
    adapters.network_ifname = network_ifname;
    return adapters;
}

}  // namespace live_stream
