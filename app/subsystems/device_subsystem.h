#ifndef LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_

#include <memory>

#include "alarm.h"
#include "network_config.h"
#include "platform/linux/platform_factory.h"
#include "system.h"
#include "time_api.h"
#include "upgrade.h"

namespace live_stream {

class CoreSubsystem;

struct DeviceRefs {
    ISystem *system = nullptr;
    ITime *time = nullptr;
    INetworkConfig *network = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
};

class DeviceSubsystem {
public:
    static DeviceSubsystem &Get();

    // Start receives pre-built platform adapters so that platform creation is
    // centralised in the caller (app_runtime.cpp / platform_factory.cpp).
    bool Start(CoreSubsystem &core_subsystem, PlatformAdapters adapters);
    void Stop();
    DeviceRefs refs() const;

private:
    DeviceSubsystem() = default;
    ~DeviceSubsystem() = default;

    DeviceSubsystem(const DeviceSubsystem &) = delete;
    DeviceSubsystem &operator=(const DeviceSubsystem &) = delete;

    std::unique_ptr<ISystemPlatform> system_platform_;
    std::unique_ptr<ITimePlatform> time_platform_;
    std::unique_ptr<INetworkPlatform> network_platform_;
    std::unique_ptr<IUpgradePlatform> upgrade_platform_;
    std::unique_ptr<ISystem> system_;
    std::unique_ptr<ITime> time_;
    std::unique_ptr<INetworkConfig> network_;
    std::unique_ptr<IAlarm> alarm_;
    std::unique_ptr<IUpgrade> upgrade_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_
