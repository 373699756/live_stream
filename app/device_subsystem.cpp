#include "device_subsystem.h"

#include "core_services.h"
#include "infra/log.h"

namespace live_stream {

DeviceSubsystem &DeviceSubsystem::Get() {
    static DeviceSubsystem subsystem;
    return subsystem;
}

bool DeviceSubsystem::Start(CoreServices &core_services,
                            PlatformAdapters adapters) {
    if (started_) {
        return true;
    }

    system_platform_ = std::move(adapters.system);
    SystemOptions system_options;
    system_options.config = core_services.config();
    system_options.event = core_services.event();
    system_options.logger = core_services.logger();
    system_options.platform = system_platform_.get();
    system_ = CreateSystem(system_options);
    if (!system_ || !system_->Start()) {
        INFRA_LOG_ERROR("app", "Start system service failed");
        Stop();
        return false;
    }

    time_platform_ = std::move(adapters.time);
    TimeOptions time_options;
    time_options.config = core_services.config();
    time_options.event = core_services.event();
    time_options.logger = core_services.logger();
    time_options.platform = time_platform_.get();
    time_options.default_ntp_config.enabled = false;
    time_ = CreateTime(time_options);
    if (!time_ || !time_->Start()) {
        INFRA_LOG_ERROR("app", "Start time service failed");
        Stop();
        return false;
    }

    network_platform_ = std::move(adapters.network);
    NetworkConfigOptions network_options;
    network_options.config = core_services.config();
    network_options.event = core_services.event();
    network_options.logger = core_services.logger();
    network_options.default_ifname = adapters.network_ifname;
    network_options.platform = network_platform_.get();
    network_ = CreateNetworkConfig(network_options);
    if (!network_ || !network_->Start()) {
        INFRA_LOG_ERROR("app", "Start network service failed: ifname=%s",
                        network_options.default_ifname.c_str());
        Stop();
        return false;
    }

    AlarmOptions alarm_options;
    alarm_options.config = core_services.config();
    alarm_options.event = core_services.event();
    alarm_options.logger = core_services.logger();
    alarm_ = CreateAlarm(alarm_options);
    if (!alarm_ || !alarm_->Start()) {
        INFRA_LOG_ERROR("app", "Start alarm service failed");
        Stop();
        return false;
    }

    UpgradeOptions upgrade_options;
    upgrade_options.config = core_services.config();
    upgrade_options.event = core_services.event();
    upgrade_options.logger = core_services.logger();
    upgrade_platform_ = std::move(adapters.upgrade);
    upgrade_options.platform = upgrade_platform_.get();
    upgrade_ = CreateUpgrade(upgrade_options);
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

}  // namespace live_stream
