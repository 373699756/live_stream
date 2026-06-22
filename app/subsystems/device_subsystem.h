#ifndef LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_

#include <memory>

#include "alarm.h"
#include "network_api.h"
#include "subsystems/device_platform_dependencies.h"
#include "system.h"
#include "time_api.h"
#include "upgrade.h"

namespace live_stream {

class CoreSubsystem;

struct DeviceRefs {
    ISystem *system = nullptr;
    ITime *time = nullptr;
    INetwork *network = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
};

class DeviceSubsystem {
public:
    static DeviceSubsystem &Get();

    bool Start(CoreSubsystem &core_subsystem,
               DevicePlatformDependencies dependencies);
    void Stop();
    DeviceRefs refs() const;

private:
    DeviceSubsystem() = default;
    ~DeviceSubsystem() = default;

    DeviceSubsystem(const DeviceSubsystem &) = delete;
    DeviceSubsystem &operator=(const DeviceSubsystem &) = delete;

    std::unique_ptr<ISystemPlatform> system_platform_;
    std::unique_ptr<ITimePlatform> time_platform_;
    std::unique_ptr<INetPlatform> network_platform_;
    std::unique_ptr<IUpgradePlatform> upgrade_platform_;
    std::unique_ptr<ISystem> system_;
    std::unique_ptr<ITime> time_;
    std::unique_ptr<INetwork> network_;
    std::unique_ptr<IAlarm> alarm_;
    std::unique_ptr<IUpgrade> upgrade_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_
