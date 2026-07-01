#ifndef LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_DEVICE_SUBSYSTEM_H_

#include <memory>
#include <string>

#include "alarm.h"
#include "system/network.h"
#include "system.h"
#include "system/time.h"
#include "system/upgrade.h"

namespace live_stream {

class FoundationSubsystem;

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

    bool Start(FoundationSubsystem &foundation_subsystem,
               std::unique_ptr<ISystemPlatform> system_platform,
               std::unique_ptr<ITimePlatform> time_platform,
               std::unique_ptr<INetPlatform> network_platform,
               std::unique_ptr<IUpgradePlatform> upgrade_platform,
               const std::string &network_ifname);
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
