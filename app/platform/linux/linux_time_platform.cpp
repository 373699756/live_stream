#include "platform/linux/device_platforms.h"

#include "platform/linux/linux_clock.h"
#include "platform/linux/linux_process.h"

#include <string>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::ReadSystemTimeMs;
using linux_platform::RunAny;
using linux_platform::SetSystemTimeMsInternal;

class LinuxTimePlatform : public ITimePlatform {
public:
    int64_t GetSystemTimeMs() override { return ReadSystemTimeMs(); }

    bool SetSystemTimeMs(int64_t unix_time_ms) override {
        return SetSystemTimeMsInternal(unix_time_ms);
    }

    bool SyncNtp(const std::vector<std::string> &servers,
                 int64_t *synced_time_ms) override {
        if (synced_time_ms == nullptr || servers.empty()) {
            return false;
        }
        std::vector<std::string> ntpd = {"ntpd", "-n", "-q"};
        std::vector<std::string> busybox_ntpd = {"busybox", "ntpd", "-n", "-q"};
        std::vector<std::string> ntpdate = {"ntpdate", "-b"};
        for (const std::string &server : servers) {
            ntpd.push_back("-p");
            ntpd.push_back(server);
            busybox_ntpd.push_back("-p");
            busybox_ntpd.push_back(server);
            ntpdate.push_back(server);
        }
        if (!RunAny({ntpd, busybox_ntpd, ntpdate})) {
            return false;
        }
        *synced_time_ms = ReadSystemTimeMs();
        return *synced_time_ms > 0;
    }
};

}  // namespace

std::unique_ptr<ITimePlatform> CreateTimePlatform() {
    return std::unique_ptr<ITimePlatform>(new LinuxTimePlatform());
}

}  // namespace live_stream
