#ifndef LIVE_STREAM_APP_PLATFORM_FACTORY_H_
#define LIVE_STREAM_APP_PLATFORM_FACTORY_H_

#include <memory>
#include <string>

#include "network_config.h"
#include "system.h"
#include "time_api.h"
#include "upgrade.h"

namespace live_stream {

// Aggregates all Linux platform adapters. Constructed by
// CreateLinuxPlatformAdapters() and consumed by DeviceSubsystem::Start().
struct PlatformAdapters {
    std::unique_ptr<ISystemPlatform> system;
    std::unique_ptr<ITimePlatform> time;
    std::unique_ptr<INetworkPlatform> network;
    std::unique_ptr<IUpgradePlatform> upgrade;
    // The network interface name associated with the network platform.
    std::string network_ifname;
};

// Creates all Linux-specific platform adapters in one place.
// network_ifname: the primary network interface (e.g. "eth0"). Must be
// non-empty.
PlatformAdapters CreateLinuxPlatformAdapters(const std::string& network_ifname);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PLATFORM_FACTORY_H_
