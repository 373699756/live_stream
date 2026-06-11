#include "platform/linux/device_platforms.h"

#include "config_json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "platform/linux/linux_platform_common.h"
#include "tools/sysupgrade/upgrade_flash.h"
#include "upgrade_package.h"

#include <cctype>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::ReadFirstText;
using linux_platform::ReadSystemTimeMs;

constexpr const char* kUpgradeRootPath = "/data/upgrade";
constexpr const char* kUpgradeUploadPath = "/tmp/live_stream/upgrade/uploads";
constexpr const char* kUpgradeStagePath = "/data/upgrade/staged";
constexpr const char* kSystemUpgradeStagePath = "/tmp/live_stream/upgrade/staged";
constexpr const char* kInstalledHelperPath = "/opt/app/sbin/live_sysupgrade";
constexpr const char* kTmpHelperPath = "/tmp/live_stream/upgrade/live_sysupgrade";
constexpr const char* kUpgradeLogPath = "/data/upgrade.log";
constexpr const char* kUpgradeStatusPath = "/data/upgrade_status.json";

std::string FirmwareVersionString() {
    std::string version = ReadFirstText({
        "/etc/firmware_version",
        "/etc/version",
        "/opt/app/version",
    });
    if (version.empty()) {
        version = "0.1.0";
    }
    return version;
}

std::vector<std::string> SplitVersion(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            current.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
            continue;
        }
        if (!current.empty()) {
            parts.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

bool IsDigitsOnly(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

int CompareNumericStrings(const std::string& lhs, const std::string& rhs) {
    std::size_t lhs_begin = lhs.find_first_not_of('0');
    std::size_t rhs_begin = rhs.find_first_not_of('0');
    const std::string lhs_trimmed =
        lhs_begin == std::string::npos ? "0" : lhs.substr(lhs_begin);
    const std::string rhs_trimmed =
        rhs_begin == std::string::npos ? "0" : rhs.substr(rhs_begin);
    if (lhs_trimmed.size() != rhs_trimmed.size()) {
        return lhs_trimmed.size() < rhs_trimmed.size() ? -1 : 1;
    }
    if (lhs_trimmed == rhs_trimmed) {
        return 0;
    }
    return lhs_trimmed < rhs_trimmed ? -1 : 1;
}

int CompareVersionStrings(const std::string& lhs, const std::string& rhs) {
    const std::vector<std::string> lhs_parts = SplitVersion(lhs);
    const std::vector<std::string> rhs_parts = SplitVersion(rhs);
    const std::size_t part_count =
        lhs_parts.size() > rhs_parts.size() ? lhs_parts.size() : rhs_parts.size();
    for (std::size_t i = 0; i < part_count; ++i) {
        const std::string lhs_part = i < lhs_parts.size() ? lhs_parts[i] : "0";
        const std::string rhs_part = i < rhs_parts.size() ? rhs_parts[i] : "0";
        if (lhs_part == rhs_part) {
            continue;
        }
        const bool lhs_numeric = IsDigitsOnly(lhs_part);
        const bool rhs_numeric = IsDigitsOnly(rhs_part);
        if (lhs_numeric && rhs_numeric) {
            const int compare = CompareNumericStrings(lhs_part, rhs_part);
            if (compare != 0) {
                return compare;
            }
            continue;
        }
        return lhs_part < rhs_part ? -1 : 1;
    }
    return 0;
}

void AppendUpgradeLog(const std::string& message) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    const std::string line =
        std::to_string(ReadSystemTimeMs()) + " " + message + "\n";
    static_cast<void>(infra::File::Append(kUpgradeLogPath, line));
}

void WriteUpgradeStatus(const std::string& state,
                        uint32_t progress,
                        bool ok,
                        const std::string& version,
                        const std::string& error_message) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    ConfigJson root = ConfigJson::object();
    root["state"] = state;
    root["progress_percent"] = progress;
    root["ok"] = ok;
    root["version"] = version;
    root["error_message"] = error_message;
    const std::string tmp_path = std::string(kUpgradeStatusPath) + ".tmp";
    const int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }
    const std::string data = root.dump(2) + "\n";
    std::size_t offset = 0;
    bool write_ok = true;
    while (offset < data.size()) {
        const ssize_t write_size =
            write(fd, data.data() + offset, data.size() - offset);
        if (write_size <= 0) {
            write_ok = false;
            break;
        }
        offset += static_cast<std::size_t>(write_size);
    }
    if (write_ok && fsync(fd) != 0) {
        write_ok = false;
    }
    close(fd);
    if (!write_ok || rename(tmp_path.c_str(), kUpgradeStatusPath) != 0) {
        static_cast<void>(infra::File::Remove(tmp_path));
        return;
    }
    const int dir_fd = open("/data", O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        static_cast<void>(fsync(dir_fd));
        close(dir_fd);
    }
}

bool ApplyWebUpgrade(const UpgradeManifest& manifest,
                     const std::string& stage_dir,
                     UpgradeProgressCallback progress_callback,
                     std::string* reason) {
    for (const UpgradeCommand& command : manifest.commands) {
        if (command.partition != "web") {
            if (reason != nullptr) {
                *reason = "only web upgrade is supported online";
            }
            return false;
        }
    }
    for (std::size_t i = 0; i < manifest.commands.size(); ++i) {
        const UpgradeCommand& command = manifest.commands[i];
        const std::string image_path = infra::Path::Join(stage_dir, command.file);
        if (!upgrade_flash::UnmountIfMounted(command.partition_info.mount_point,
                                             reason)) {
            return false;
        }
        if (!upgrade_flash::WriteMtdImage(command, image_path, reason)) {
            static_cast<void>(upgrade_flash::Remount(command.partition_info,
                                                     nullptr));
            return false;
        }
        if (!upgrade_flash::Remount(command.partition_info, reason)) {
            return false;
        }
        if (progress_callback) {
            const uint32_t progress = static_cast<uint32_t>(
                ((i + 1) * 100ULL) / manifest.commands.size());
            progress_callback(infra::Clamp<uint32_t>(progress, 0U, 100U));
        }
    }
    return true;
}

bool CopyFile(const std::string& from_path,
              const std::string& to_path,
              std::string* reason) {
    const int from_fd = open(from_path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (from_fd < 0) {
        if (reason != nullptr) {
            *reason = "open helper failed";
        }
        return false;
    }
    struct stat from_stat;
    if (fstat(from_fd, &from_stat) != 0 || !S_ISREG(from_stat.st_mode)) {
        close(from_fd);
        if (reason != nullptr) {
            *reason = "helper is not a regular file";
        }
        return false;
    }
    if (!infra::Path::MakeDirs(infra::Path::DirName(to_path))) {
        close(from_fd);
        if (reason != nullptr) {
            *reason = "create helper directory failed";
        }
        return false;
    }
    static_cast<void>(infra::File::Remove(to_path));
    const int to_fd = open(to_path.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0755);
    if (to_fd < 0) {
        close(from_fd);
        if (reason != nullptr) {
            *reason = "create helper copy failed";
        }
        return false;
    }
    uint8_t buffer[64 * 1024];
    bool ok = true;
    while (ok) {
        const ssize_t read_size = read(from_fd, buffer, sizeof(buffer));
        if (read_size < 0) {
            ok = false;
            break;
        }
        if (read_size == 0) {
            break;
        }
        ssize_t offset = 0;
        while (offset < read_size) {
            const ssize_t write_size =
                write(to_fd, buffer + offset,
                      static_cast<std::size_t>(read_size - offset));
            if (write_size <= 0) {
                ok = false;
                break;
            }
            offset += write_size;
        }
    }
    if (ok && fsync(to_fd) != 0) {
        ok = false;
    }
    if (ok && fchmod(to_fd, 0755) != 0) {
        ok = false;
    }
    close(to_fd);
    close(from_fd);
    if (!ok) {
        if (reason != nullptr) {
            *reason = "copy helper failed";
        }
        return false;
    }
    return true;
}

bool StartSystemUpgradeHelper(const ParsedUpgradePackage& package,
                              const std::string& helper_path,
                              std::string* reason) {
    if (!upgrade_flash::IsPathOnTmpfs("/tmp/live_stream/upgrade")) {
        if (reason != nullptr) {
            *reason = "/tmp/live_stream/upgrade is not tmpfs";
        }
        return false;
    }
    if (!upgrade_flash::ValidateMtdLayoutForManifest(package.manifest, reason)) {
        return false;
    }
    if (!infra::Path::MakeDirs(kSystemUpgradeStagePath)) {
        if (reason != nullptr) {
            *reason = "create tmp stage directory failed";
        }
        return false;
    }
    if (!CopyFile(kInstalledHelperPath, helper_path, reason)) {
        return false;
    }
    if (chmod(helper_path.c_str(), 0755) != 0) {
        if (reason != nullptr) {
            *reason = "chmod helper failed";
        }
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (reason != nullptr) {
            *reason = "fork helper failed";
        }
        return false;
    }
    if (pid == 0) {
        setsid();
        execl(helper_path.c_str(), helper_path.c_str(), "--package",
              package.package_path.c_str(), "--stage", kSystemUpgradeStagePath,
              "--reboot", nullptr);
        _exit(127);
    }
    AppendUpgradeLog("system upgrade helper started: " +
                     std::to_string(static_cast<int64_t>(pid)));
    return true;
}

class UpgradePlatform : public IUpgradePlatform {
public:
    UpgradePackageInfo ValidatePackage(const std::string& package_path) override {
        ParsedUpgradePackage parsed;
        std::string reason;
        if (!ParseUpgradePackage(package_path, &parsed, &reason)) {
            AppendUpgradeLog("validate failed: " + reason);
            return UpgradePackageInfo();
        }
        cached_package_ = parsed;
        UpgradePackageInfo info;
        info.package_path = parsed.package_path;
        info.version = parsed.manifest.version;
        info.size_bytes = parsed.size_bytes;
        info.digest = parsed.sha256;
        info.build_time_ms = parsed.mtime_ms;
        info.target_model = parsed.manifest.board;
        info.requires_reboot = parsed.requires_reboot;
        return info;
    }

    std::string GetCurrentVersion() override { return FirmwareVersionString(); }

    int CompareVersion(const std::string& lhs,
                       const std::string& rhs) override {
        return CompareVersionStrings(lhs, rhs);
    }

    bool PrepareUpgrade(const UpgradePackageInfo& info) override {
        if (info.package_path.empty()) {
            return false;
        }
        ParsedUpgradePackage parsed;
        std::string reason;
        if (!ParseUpgradePackage(info.package_path, &parsed, &reason)) {
            AppendUpgradeLog("prepare failed: " + reason);
            WriteUpgradeStatus("failed", 100, false, info.version, reason);
            return false;
        }
        if (!infra::Path::MakeDirs(kUpgradeRootPath) ||
            !infra::Path::MakeDirs(kUpgradeStagePath) ||
            !infra::Path::MakeDirs(kUpgradeUploadPath)) {
            return false;
        }
        cancel_requested_ = false;
        cached_package_ = parsed;
        system_upgrade_ = !UpgradePackageIsWebOnly(parsed.manifest);
        if (system_upgrade_) {
            stage_dir_.clear();
            if (!upgrade_flash::IsPathOnTmpfs("/tmp/live_stream/upgrade")) {
                reason = "/tmp/live_stream/upgrade is not tmpfs";
                AppendUpgradeLog("prepare failed: " + reason);
                WriteUpgradeStatus("failed", 100, false, info.version, reason);
                return false;
            }
            if (!upgrade_flash::ValidateMtdLayoutForManifest(parsed.manifest,
                                                             &reason)) {
                AppendUpgradeLog("prepare failed: " + reason);
                WriteUpgradeStatus("failed", 100, false, info.version, reason);
                return false;
            }
            return true;
        }
        stage_dir_ = infra::Path::Join(
            kUpgradeStagePath,
            std::to_string(ReadSystemTimeMs()) + "-" + parsed.manifest.version);
        return infra::Path::MakeDirs(stage_dir_);
    }

    bool WriteUpgrade(const std::string& package_path,
                      UpgradeProgressCallback progress_callback) override {
        if (package_path.empty()) {
            return false;
        }
        if (cancel_requested_) {
            return false;
        }
        if (system_upgrade_) {
            std::string reason;
            if (!StartSystemUpgradeHelper(cached_package_, kTmpHelperPath,
                                          &reason)) {
                AppendUpgradeLog("start helper failed: " + reason);
                WriteUpgradeStatus("failed", 100, false,
                                   cached_package_.manifest.version, reason);
                return false;
            }
            WriteUpgradeStatus("writing", 80, true,
                               cached_package_.manifest.version, "");
            if (progress_callback) {
                progress_callback(100);
            }
            return true;
        }
        if (stage_dir_.empty()) {
            return false;
        }
        std::string reason;
        const bool extract_ok = ExtractUpgradeFiles(
            package_path, cached_package_.manifest, stage_dir_,
            [progress_callback](uint32_t progress) {
                if (progress_callback) {
                    progress_callback(progress / 2);
                }
            },
            &reason);
        if (!extract_ok) {
            AppendUpgradeLog("extract failed: " + reason);
            return false;
        }
        if (cancel_requested_) {
            return false;
        }
        const bool write_ok = ApplyWebUpgrade(
            cached_package_.manifest, stage_dir_,
            [progress_callback](uint32_t progress) {
                if (progress_callback) {
                    progress_callback(50U + progress / 2);
                }
            },
            &reason);
        if (!write_ok) {
            AppendUpgradeLog("write failed: " + reason);
            WriteUpgradeStatus("failed", 100, false,
                               cached_package_.manifest.version, reason);
        }
        return write_ok;
    }

    bool CommitUpgrade(const UpgradePackageInfo& info) override {
        if (info.version.empty()) {
            return false;
        }
        if (system_upgrade_) {
            WriteUpgradeStatus("waiting_reboot", 100, true, info.version, "");
            AppendUpgradeLog("system upgrade delegated to RAM helper: " +
                             info.version);
            return true;
        }
        WriteUpgradeStatus(info.requires_reboot ? "waiting_reboot" : "completed",
                           100, true, info.version, "");
        AppendUpgradeLog("web upgrade completed: " + info.version);
        return true;
    }

    bool CancelUpgrade() override {
        cancel_requested_ = true;
        return true;
    }

    bool RebootToApply() override {
        if (system_upgrade_) {
            AppendUpgradeLog("reboot is delegated to RAM helper");
            return true;
        }
        sync();
        return reboot(RB_AUTOBOOT) == 0;
    }

    bool CleanupFailedUpgrade() override {
        AppendUpgradeLog("upgrade cleanup after failure");
        return true;
    }

private:
    bool cancel_requested_ = false;
    bool system_upgrade_ = false;
    ParsedUpgradePackage cached_package_;
    std::string stage_dir_;
};

}  // namespace

std::unique_ptr<IUpgradePlatform> CreateUpgradePlatform() {
    return std::unique_ptr<IUpgradePlatform>(new UpgradePlatform());
}

}  // namespace live_stream
