#include "device_subsystem.h"

#include "core_services.h"
#include "device_platforms.h"
#include "infra/log.h"

namespace live_stream {

DeviceSubsystem &DeviceSubsystem::Get() {
  static DeviceSubsystem subsystem;
  return subsystem;
}

bool DeviceSubsystem::Start() {
  if (started_) {
    return true;
  }

  CoreServices &core = CoreServices::Get();

  system_platform_ = CreateLinuxSystemPlatform();
  SystemServiceOptions system_options;
  system_options.config_service = core.config();
  system_options.event_service = core.event();
  system_options.logger_service = core.logger();
  system_options.platform = system_platform_.get();
  system_ = CreateSystemService(system_options);
  if (!system_ || !system_->Start()) {
    INFRA_LOG_ERROR("app", "Start system service failed");
    Stop();
    return false;
  }

  time_platform_ = CreateLinuxTimePlatform();
  TimeServiceOptions time_options;
  time_options.config_service = core.config();
  time_options.event_service = core.event();
  time_options.logger_service = core.logger();
  time_options.platform = time_platform_.get();
  time_options.default_ntp_config.enabled = false;
  time_ = CreateTimeService(time_options);
  if (!time_ || !time_->Start()) {
    INFRA_LOG_ERROR("app", "Start time service failed");
    Stop();
    return false;
  }

  network_platform_ = CreateLinuxNetworkPlatform("eth0");
  NetworkServiceOptions network_options;
  network_options.config_service = core.config();
  network_options.event_service = core.event();
  network_options.logger_service = core.logger();
  network_options.default_ifname = "eth0";
  network_options.platform = network_platform_.get();
  network_ = CreateNetworkService(network_options);
  if (!network_ || !network_->Start()) {
    INFRA_LOG_ERROR("app", "Start network service failed: ifname=%s",
                    network_options.default_ifname.c_str());
    Stop();
    return false;
  }

  AlarmServiceOptions alarm_options;
  alarm_options.config_service = core.config();
  alarm_options.event_service = core.event();
  alarm_options.logger_service = core.logger();
  alarm_ = CreateAlarmService(alarm_options);
  if (!alarm_ || !alarm_->Start()) {
    INFRA_LOG_ERROR("app", "Start alarm service failed");
    Stop();
    return false;
  }

  UpgradeServiceOptions upgrade_options;
  upgrade_options.config_service = core.config();
  upgrade_options.event_service = core.event();
  upgrade_options.logger_service = core.logger();
  upgrade_platform_ = CreateLinuxUpgradePlatform();
  upgrade_options.platform = upgrade_platform_.get();
  upgrade_ = CreateUpgradeService(upgrade_options);
  if (!upgrade_ || !upgrade_->Start()) {
    INFRA_LOG_ERROR("app", "Start upgrade service failed");
    Stop();
    return false;
  }

  started_ = true;
  return true;
}

void DeviceSubsystem::Stop() {
  if (upgrade_) {
    upgrade_->Stop();
    upgrade_.reset();
  }
  upgrade_platform_.reset();
  if (alarm_) {
    alarm_->Stop();
    alarm_.reset();
  }
  if (network_) {
    network_->Stop();
    network_.reset();
  }
  if (time_) {
    time_->Stop();
    time_.reset();
  }
  if (system_) {
    system_->Stop();
    system_.reset();
  }
  network_platform_.reset();
  time_platform_.reset();
  system_platform_.reset();
  started_ = false;
}

DeviceRefs DeviceSubsystem::refs() const {
  DeviceRefs refs;
  refs.system = system_.get();
  refs.time = time_.get();
  refs.network = network_.get();
  refs.alarm = alarm_.get();
  refs.upgrade = upgrade_.get();
  return refs;
}

} // namespace live_stream
