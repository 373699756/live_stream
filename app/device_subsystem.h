#ifndef LIVE_STREAM_APP_DEVICE_SUBSYSTEM_H_
#define LIVE_STREAM_APP_DEVICE_SUBSYSTEM_H_

#include <memory>

#include "alarm_service.h"
#include "network_service.h"
#include "system_service.h"
#include "time_service.h"
#include "upgrade_service.h"

namespace live_stream {

struct DeviceRefs {
  ISystemService *system = nullptr;
  ITimeService *time = nullptr;
  INetworkService *network = nullptr;
  IAlarmService *alarm = nullptr;
  IUpgradeService *upgrade = nullptr;
};

class DeviceSubsystem {
public:
  static DeviceSubsystem &Get();

  bool Start();
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
  std::unique_ptr<ISystemService> system_;
  std::unique_ptr<ITimeService> time_;
  std::unique_ptr<INetworkService> network_;
  std::unique_ptr<IAlarmService> alarm_;
  std::unique_ptr<IUpgradeService> upgrade_;
  bool started_ = false;
};

} // namespace live_stream

#endif // LIVE_STREAM_APP_DEVICE_SUBSYSTEM_H_
