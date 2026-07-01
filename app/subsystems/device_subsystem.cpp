#include "subsystems/device_subsystem.h"

#include "infra/log.h"
#include "subsystems/foundation_subsystem.h"

namespace live_stream {

DeviceSubsystem &DeviceSubsystem::Get() {
    static DeviceSubsystem subsystem;
    return subsystem;
}

bool DeviceSubsystem::Start(FoundationSubsystem &foundation_subsystem,
                            std::unique_ptr<ISystemPlatform> system_platform,
                            std::unique_ptr<ITimePlatform> time_platform,
                            std::unique_ptr<INetPlatform> network_platform,
                            std::unique_ptr<IUpgradePlatform> upgrade_platform,
                            const std::string &network_ifname) {
    if (started_) {
        return true;
    }

    system_platform_ = std::move(system_platform);
    SystemOptions system_options;
    system_options.config = foundation_subsystem.config();
    system_options.event = foundation_subsystem.event();
    system_options.logger = foundation_subsystem.logger();
    system_options.platform = system_platform_.get();
    system_ = CreateSystem(system_options);
    if (!system_ || !system_->Start()) {
        Error("app", "Start system failed");
        Stop();
        return false;
    }

    time_platform_ = std::move(time_platform);
    TimeOptions time_options;
    time_options.config = foundation_subsystem.config();
    time_options.event = foundation_subsystem.event();
    time_options.logger = foundation_subsystem.logger();
    time_options.platform = time_platform_.get();
    time_options.default_ntp_config.enabled = false;
    time_ = CreateTime(time_options);
    if (!time_ || !time_->Start()) {
        Error("app", "Start time failed");
        Stop();
        return false;
    }

    network_platform_ = std::move(network_platform);
    NetOptions network_options;
    network_options.config = foundation_subsystem.config();
    network_options.event = foundation_subsystem.event();
    network_options.logger = foundation_subsystem.logger();
    network_options.default_ifname = network_ifname;
    network_options.platform = network_platform_.get();
    network_ = CreateNetwork(network_options);
    if (!network_ || !network_->Start()) {
        Error("app", "Start network failed: ifname=%s",
              network_options.default_ifname.c_str());
        Stop();
        return false;
    }

    AlarmOptions alarm_options;
    alarm_options.config = foundation_subsystem.config();
    alarm_options.event = foundation_subsystem.event();
    alarm_options.logger = foundation_subsystem.logger();
    alarm_ = CreateAlarm(alarm_options);
    if (!alarm_ || !alarm_->Start()) {
        Error("app", "Start alarm failed");
        Stop();
        return false;
    }

    UpgradeOptions upgrade_options;
    upgrade_options.config = foundation_subsystem.config();
    upgrade_options.event = foundation_subsystem.event();
    upgrade_options.logger = foundation_subsystem.logger();
    upgrade_platform_ = std::move(upgrade_platform);
    upgrade_options.platform = upgrade_platform_.get();
    upgrade_ = CreateUpgrade(upgrade_options);
    if (!upgrade_ || !upgrade_->Start()) {
        Error("app", "Start upgrade failed");
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

}  // namespace live_stream
