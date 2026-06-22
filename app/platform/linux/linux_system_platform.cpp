#include "platform/linux/device_platforms.h"

#include "infra/fs.h"
#include "platform/linux/linux_platform_common.h"

#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/reboot.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::IsExecutable;
using linux_platform::ReadFirstText;
using linux_platform::RunAny;
using linux_platform::RunCommand;
using linux_platform::Trim;

struct CpuTimes {
    uint64_t total = 0;
    uint64_t idle = 0;
};

std::string ReadCpuInfoValue(const std::string &key) {
    const std::string content = infra::File::ReadAll("/proc/cpuinfo");
    if (content.empty()) {
        return std::string();
    }
    std::istringstream stream(content);
    std::string line;
    const std::string prefix = key + ":";
    while (std::getline(stream, line)) {
        if (line.compare(0, prefix.size(), prefix) == 0) {
            return Trim(line.substr(prefix.size()));
        }
    }
    return std::string();
}

bool ReadCpuTimes(CpuTimes *times) {
    if (times == nullptr) {
        return false;
    }
    const std::string content = infra::File::ReadAll("/proc/stat");
    if (content.empty()) {
        return false;
    }
    std::istringstream stream(content);
    std::string cpu_label;
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;
    stream >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >>
        softirq >> steal;
    if (!stream || cpu_label != "cpu") {
        return false;
    }
    times->idle = idle + iowait;
    times->total = user + nice + system + idle + iowait + irq + softirq + steal;
    return true;
}

uint32_t ReadMemoryUsagePercent() {
    const std::string content = infra::File::ReadAll("/proc/meminfo");
    if (content.empty()) {
        return 0;
    }
    std::istringstream stream(content);
    std::string key;
    int64_t value = 0;
    std::string unit;
    int64_t total_kb = 0;
    int64_t available_kb = 0;
    while (stream >> key >> value >> unit) {
        if (key == "MemTotal:") {
            total_kb = value;
        } else if (key == "MemAvailable:") {
            available_kb = value;
        }
    }
    if (total_kb <= 0 || available_kb < 0 || available_kb > total_kb) {
        return 0;
    }
    return static_cast<uint32_t>((total_kb - available_kb) * 100 / total_kb);
}

int32_t ReadTemperatureCelsius() {
    int64_t raw = 0;
    std::istringstream stream(
        infra::File::ReadAll("/sys/class/thermal/thermal_zone0/temp"));
    stream >> raw;
    if (!stream) {
        return 0;
    }
    if (raw > 1000) {
        raw /= 1000;
    }
    return static_cast<int32_t>(raw);
}

int64_t ReadUptimeMs() {
    std::istringstream stream(infra::File::ReadAll("/proc/uptime"));
    double uptime_sec = 0.0;
    stream >> uptime_sec;
    if (!stream) {
        return 0;
    }
    return static_cast<int64_t>(uptime_sec * 1000.0);
}

std::string FindFactoryResetScript() {
    static const char *const kPaths[] = {
        "/usr/bin/factory_reset.sh",
        "/usr/sbin/factory_reset.sh",
        "/etc/init.d/factory_reset",
        "/usr/bin/factory_reset",
        "/usr/sbin/factory_reset",
    };
    for (const char *path : kPaths) {
        if (IsExecutable(path)) {
            return path;
        }
    }
    return std::string();
}

class LinuxSystemPlatform : public ISystemPlatform {
public:
    DeviceInfo GetDeviceInfo() override {
        DeviceInfo info;
        info.model = ReadFirstText({
            "/proc/device-tree/model",
            "/sys/firmware/devicetree/base/model",
        });
        info.serial_number = ReadFirstText({
            "/proc/device-tree/serial-number",
            "/sys/firmware/devicetree/base/serial-number",
        });
        if (info.serial_number.empty()) {
            info.serial_number = ReadCpuInfoValue("Serial");
        }
        info.firmware_version = ReadFirstText({
            "/etc/firmware_version",
            "/etc/version",
        });
        if (info.model.empty()) {
            info.model = "live_stream_ipc";
        }
        if (info.serial_number.empty()) {
            info.serial_number = "unknown";
        }
        if (info.firmware_version.empty()) {
            info.firmware_version = "0.1.0";
        }
        return info;
    }

    SystemStatus GetSystemStatus() override {
        SystemStatus status;
        status.cpu_usage_percent = ReadCpuUsagePercent();
        status.memory_usage_percent = ReadMemoryUsagePercent();
        status.temperature_celsius = ReadTemperatureCelsius();
        status.uptime_ms = ReadUptimeMs();
        status.healthy = true;
        return status;
    }

    SystemCapabilities GetCapabilities() override {
        SystemCapabilities caps;
        caps.supports_reboot =
            IsExecutable("/sbin/reboot") || IsExecutable("/bin/reboot") ||
            IsExecutable("/bin/busybox") || IsExecutable("/sbin/busybox");
        caps.supports_factory_reset = !FindFactoryResetScript().empty();
        caps.features.push_back("heartbeat");
        caps.features.push_back("system_status");
        caps.features.push_back("time_sync");
        caps.features.push_back("system.network");
        if (caps.supports_reboot) {
            caps.features.push_back("reboot");
        }
        if (caps.supports_factory_reset) {
            caps.features.push_back("factory_reset");
        }
        return caps;
    }

    bool Reboot() override {
        sync();
        if (reboot(RB_AUTOBOOT) == 0) {
            return true;
        }
        return RunAny({
            {"reboot"},
            {"/sbin/reboot"},
            {"busybox", "reboot"},
            {"/bin/busybox", "reboot"},
        });
    }

    bool FactoryReset() override {
        const std::string script = FindFactoryResetScript();
        if (script.empty()) {
            return false;
        }
        sync();
        return RunCommand({script}) == 0;
    }

private:
    uint32_t ReadCpuUsagePercent() {
        CpuTimes sample;
        if (!ReadCpuTimes(&sample)) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cpu_valid_) {
            last_cpu_ = sample;
            cpu_valid_ = true;
            return 0;
        }
        const uint64_t total_delta = sample.total - last_cpu_.total;
        const uint64_t idle_delta = sample.idle - last_cpu_.idle;
        last_cpu_ = sample;
        if (total_delta == 0 || idle_delta > total_delta) {
            return 0;
        }
        return static_cast<uint32_t>(((total_delta - idle_delta) * 100ULL) /
                                     total_delta);
    }

    std::mutex mutex_;
    CpuTimes last_cpu_;
    bool cpu_valid_ = false;
};

}  // namespace

std::unique_ptr<ISystemPlatform> CreateSystemPlatform() {
    return std::unique_ptr<ISystemPlatform>(new LinuxSystemPlatform());
}

}  // namespace live_stream
