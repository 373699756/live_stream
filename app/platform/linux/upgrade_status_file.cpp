#include "platform/linux/upgrade_status_file.h"

#include "infra/fs.h"
#include "infra/time.h"

#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace live_stream {
namespace linux_platform {
namespace {

constexpr const char* kUpgradeDataPath = "/data";
constexpr const char* kUpgradeLogPath = "/data/upgrade.log";
constexpr const char* kUpgradeStatusPath = "/data/upgrade_status.json";
constexpr uint64_t kUpgradeLogMaxBytes = 64U * 1024U;
constexpr uint32_t kUpgradeLogRotateFiles = 1;

bool WriteAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t write_size =
            write(fd, data.data() + offset, data.size() - offset);
        if (write_size <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(write_size);
    }
    return true;
}

void FsyncUpgradeDataDir() {
    const int dir_fd = open(kUpgradeDataPath, O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        return;
    }
    static_cast<void>(fsync(dir_fd));
    close(dir_fd);
}

}  // namespace

void AppendUpgradeLogLine(const std::string& message) {
    static_cast<void>(infra::Path::MakeDirs(kUpgradeDataPath));
    if (infra::File::Size(kUpgradeLogPath) >= kUpgradeLogMaxBytes) {
        const std::string rotated_path =
            std::string(kUpgradeLogPath) + "." +
            std::to_string(kUpgradeLogRotateFiles);
        static_cast<void>(infra::File::Remove(rotated_path));
        static_cast<void>(infra::File::Rename(kUpgradeLogPath, rotated_path));
    }
    const std::string line =
        std::to_string(infra::Time::SystemTimeMillis()) + " " + message + "\n";
    static_cast<void>(infra::File::Append(kUpgradeLogPath, line));
}

bool WriteUpgradeStatusFile(const Json& status) {
    if (!infra::Path::MakeDirs(kUpgradeDataPath)) {
        return false;
    }
    const std::string tmp_path = std::string(kUpgradeStatusPath) + ".tmp";
    const int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    const std::string data = status.dump(2) + "\n";
    bool write_ok = WriteAll(fd, data);
    if (write_ok && fsync(fd) != 0) {
        write_ok = false;
    }
    close(fd);
    if (!write_ok || rename(tmp_path.c_str(), kUpgradeStatusPath) != 0) {
        static_cast<void>(infra::File::Remove(tmp_path));
        return false;
    }
    FsyncUpgradeDataDir();
    return true;
}

}  // namespace linux_platform
}  // namespace live_stream
