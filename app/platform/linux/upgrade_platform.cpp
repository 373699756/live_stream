#include "platform/linux/device_platforms.h"

#include "json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "platform/linux/linux_platform_common.h"
#include "tools/sysupgrade/upgrade_flash.h"
#include "system/package.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <dirent.h>
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
constexpr const char* kUpgradeLogPath = "/data/upgrade.log";
constexpr const char* kUpgradeInfoPath = "/data/upgrade_status.json";

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
    const std::size_t part_size =
        lhs_parts.size() > rhs_parts.size() ? lhs_parts.size() : rhs_parts.size();
    for (std::size_t i = 0; i < part_size; ++i) {
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

void AppendUpgradeLog(const std::string& msg) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    const std::string line =
        std::to_string(ReadSystemTimeMs()) + " " + msg + "\n";
    static_cast<void>(infra::File::Append(kUpgradeLogPath, line));
}

void WriteUpgradeInfo(const std::string& state,
                      uint32_t progress,
                      bool ok,
                      const std::string& version,
                      const std::string& error_message) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    Json root = Json::object();
    root["state"] = state;
    root["progress_percent"] = progress;
    root["ok"] = ok;
    root["version"] = version;
    root["error_message"] = error_message;
    const std::string tmp_path = std::string(kUpgradeInfoPath) + ".tmp";
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
    if (!write_ok || rename(tmp_path.c_str(), kUpgradeInfoPath) != 0) {
        static_cast<void>(infra::File::Remove(tmp_path));
        return;
    }
    const int dir_fd = open("/data", O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        static_cast<void>(fsync(dir_fd));
        close(dir_fd);
    }
}

bool RemovePathTree(const std::string& path) {
    struct stat path_stat;
    if (path.empty() || lstat(path.c_str(), &path_stat) != 0) {
        return false;
    }
    if (!S_ISDIR(path_stat.st_mode)) {
        return unlink(path.c_str()) == 0;
    }

    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    bool ok = true;
    while (true) {
        errno = 0;
        struct dirent* entry = readdir(dir);
        if (entry == nullptr) {
            if (errno != 0) {
                ok = false;
            }
            break;
        }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!RemovePathTree(infra::Path::Join(path, name))) {
            ok = false;
        }
    }
    if (closedir(dir) != 0) {
        ok = false;
    }
    if (rmdir(path.c_str()) != 0) {
        ok = false;
    }
    return ok;
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

class UpgradePlatform : public IUpgradePlatform {
public:
    UpgradePackageInfo ValidatePackage(const std::string& package_path) override {
        last_error_.clear();
        ParsedUpgradePackage parsed;
        std::string reason;
        if (!ParseUpgradePackage(package_path, &parsed, &reason)) {
            last_error_ = reason;
            AppendUpgradeLog("validate failed: " + reason);
            return UpgradePackageInfo();
        }
        if (!UpgradePackageIsWebOnly(parsed.manifest)) {
            last_error_ = "Web upgrade only accepts web partition packages";
            AppendUpgradeLog(
                "validate failed: Web upgrade only accepts web partition packages");
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
        last_error_.clear();
        if (info.package_path.empty()) {
            last_error_ = "package path is empty";
            return false;
        }
        ParsedUpgradePackage parsed;
        std::string reason;
        if (!ParseUpgradePackage(info.package_path, &parsed, &reason)) {
            last_error_ = reason;
            AppendUpgradeLog("prepare failed: " + reason);
            WriteUpgradeInfo("failed", 100, false, info.version, reason);
            return false;
        }
        if (!UpgradePackageIsWebOnly(parsed.manifest)) {
            reason = "Web upgrade only accepts web partition packages";
            last_error_ = reason;
            AppendUpgradeLog("prepare failed: " + reason);
            WriteUpgradeInfo("failed", 100, false, info.version, reason);
            return false;
        }
        if (!infra::Path::MakeDirs(kUpgradeRootPath) ||
            !infra::Path::MakeDirs(kUpgradeStagePath) ||
            !infra::Path::MakeDirs(kUpgradeUploadPath)) {
            last_error_ = "create upgrade directory failed";
            return false;
        }
        cancel_requested_ = false;
        cached_package_ = parsed;
        stage_dir_ = infra::Path::Join(
            kUpgradeStagePath,
            std::to_string(ReadSystemTimeMs()) + "-" + parsed.manifest.version);
        if (!infra::Path::MakeDirs(stage_dir_)) {
            last_error_ = "create stage directory failed";
            return false;
        }
        return true;
    }

    bool WriteUpgrade(const std::string& package_path,
                      UpgradeProgressCallback progress_callback) override {
        last_error_.clear();
        if (package_path.empty()) {
            last_error_ = "package path is empty";
            return false;
        }
        if (cancel_requested_) {
            last_error_ = "upgrade canceled";
            return false;
        }
        if (stage_dir_.empty()) {
            last_error_ = "stage directory is empty";
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
            last_error_ = reason;
            AppendUpgradeLog("extract failed: " + reason);
            return false;
        }
        if (cancel_requested_) {
            last_error_ = "upgrade canceled";
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
            last_error_ = reason;
            AppendUpgradeLog("write failed: " + reason);
            WriteUpgradeInfo("failed", 100, false,
                             cached_package_.manifest.version, reason);
        }
        return write_ok;
    }

    bool CommitUpgrade(const UpgradePackageInfo& info) override {
        last_error_.clear();
        if (info.version.empty()) {
            last_error_ = "upgrade version is empty";
            return false;
        }
        WriteUpgradeInfo(info.requires_reboot ? "waiting_reboot" : "completed",
                         100, true, info.version, "");
        AppendUpgradeLog("web upgrade completed: " + info.version);
        CleanupStageDir();
        return true;
    }

    bool CancelUpgrade() override {
        last_error_.clear();
        cancel_requested_ = true;
        return true;
    }

    bool RebootToApply() override {
        last_error_.clear();
        sync();
        if (reboot(RB_AUTOBOOT) != 0) {
            last_error_ = "reboot failed";
            return false;
        }
        return true;
    }

    bool CleanupFailedUpgrade() override {
        AppendUpgradeLog("upgrade cleanup after failure");
        CleanupStageDir();
        return true;
    }

    std::string LastError() override {
        return last_error_;
    }

private:
    void CleanupStageDir() {
        if (stage_dir_.empty()) {
            return;
        }
        const std::string stage_prefix = std::string(kUpgradeStagePath) + "/";
        if (stage_dir_.compare(0, stage_prefix.size(), stage_prefix) != 0) {
            return;
        }
        static_cast<void>(RemovePathTree(stage_dir_));
        stage_dir_.clear();
    }

    bool cancel_requested_ = false;
    ParsedUpgradePackage cached_package_;
    std::string stage_dir_;
    std::string last_error_;
};

}  // namespace

std::unique_ptr<IUpgradePlatform> CreateUpgradePlatform() {
    return std::unique_ptr<IUpgradePlatform>(new UpgradePlatform());
}

}  // namespace live_stream
