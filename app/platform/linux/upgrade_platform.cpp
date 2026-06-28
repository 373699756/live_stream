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

constexpr const char* kUpgradeRootPath = "/tmp/live_stream/upgrade";
constexpr const char* kUpgradeUploadPath = "/tmp/live_stream/upgrade/uploads";
constexpr const char* kUpgradeStagePath = "/tmp/live_stream/upgrade/staged";
constexpr const char* kUpgradeLogPath = "/data/log/upgrade.log";
constexpr const char* kUpgradeInfoPath = "/data/upgrade_status.json";
constexpr const char* kSystemUpgradeRuntimePath = "/tmp/live_stream/upgrade";
constexpr const char* kSystemUpgradeStagePath =
    "/tmp/live_stream/upgrade/staged";
constexpr const char* kSystemUpgradeHelperSourcePath =
    "/opt/app/sbin/live_sysupgrade";
constexpr const char* kSystemUpgradeHelperPath =
    "/tmp/live_stream/upgrade/live_sysupgrade";
constexpr uint64_t kUpgradeLogMaxBytes = 64U * 1024U;
constexpr uint32_t kUpgradeLogRotateFiles = 1;

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
    static_cast<void>(infra::Path::MakeDirs("/data/log"));
    if (infra::File::Size(kUpgradeLogPath) >= kUpgradeLogMaxBytes) {
        const std::string rotated_path =
            std::string(kUpgradeLogPath) + "." +
            std::to_string(kUpgradeLogRotateFiles);
        static_cast<void>(infra::File::Remove(rotated_path));
        static_cast<void>(infra::File::Rename(kUpgradeLogPath, rotated_path));
    }
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
        AppendUpgradeLog("web write begin: partition=" + command.partition +
                         " file=" + command.file +
                         " image=" + image_path +
                         " size=" + std::to_string(command.size_bytes) +
                         " mtd=" + command.partition_info.mtd_path +
                         " mount=" + command.partition_info.mount_point);
        if (!upgrade_flash::UnmountIfMounted(command.partition_info.mount_point,
                                             reason)) {
            AppendUpgradeLog("web unmount failed: mount=" +
                             command.partition_info.mount_point +
                             " msg=" +
                             (reason == nullptr ? std::string() : *reason));
            return false;
        }
        if (!upgrade_flash::WriteMtdImage(command, image_path, reason)) {
            static_cast<void>(upgrade_flash::Remount(command.partition_info,
                                                     nullptr));
            AppendUpgradeLog("web mtd write failed: partition=" +
                             command.partition + " mtd=" +
                             command.partition_info.mtd_path + " msg=" +
                             (reason == nullptr ? std::string() : *reason));
            return false;
        }
        if (!upgrade_flash::Remount(command.partition_info, reason)) {
            AppendUpgradeLog("web remount failed: mount=" +
                             command.partition_info.mount_point +
                             " msg=" +
                             (reason == nullptr ? std::string() : *reason));
            return false;
        }
        AppendUpgradeLog("web write done: partition=" + command.partition +
                         " file=" + command.file);
        if (progress_callback) {
            const uint32_t progress = static_cast<uint32_t>(
                ((i + 1) * 100ULL) / manifest.commands.size());
            progress_callback(infra::Clamp<uint32_t>(progress, 0U, 100U));
        }
    }
    return true;
}

bool PackageNeedsSystemUpgradeHelper(const UpgradeManifest& manifest) {
    for (const UpgradeCommand& command : manifest.commands) {
        if (command.partition != "web") {
            return true;
        }
    }
    return false;
}

std::string UpgradeManifestSummary(const UpgradeManifest& manifest) {
    std::string summary = "version=" + manifest.version +
                          " board=" + manifest.board +
                          " flash=" + manifest.flash +
                          " reboot=" + (manifest.reboot ? "true" : "false") +
                          " commands=" +
                          std::to_string(manifest.commands.size());
    for (const UpgradeCommand& command : manifest.commands) {
        summary += " [" + command.partition + ":" + command.file +
                   " size=" + std::to_string(command.size_bytes) + "]";
    }
    return summary;
}

bool CopyFileNoSymlink(const std::string& source_path,
                       const std::string& output_path,
                       mode_t mode,
                       std::string* reason) {
    struct stat source_stat;
    if (lstat(source_path.c_str(), &source_stat) != 0 ||
        S_ISLNK(source_stat.st_mode) || !S_ISREG(source_stat.st_mode)) {
        if (reason != nullptr) {
            *reason = "system upgrade helper is not a regular file";
        }
        return false;
    }

    const int source_fd = open(source_path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (source_fd < 0) {
        if (reason != nullptr) {
            *reason = "open system upgrade helper failed";
        }
        return false;
    }

    const int output_fd = open(output_path.c_str(),
                               O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
                               mode);
    if (output_fd < 0) {
        close(source_fd);
        if (reason != nullptr) {
            *reason = "create tmpfs system upgrade helper failed";
        }
        return false;
    }

    bool ok = true;
    uint8_t buffer[64 * 1024];
    while (true) {
        const ssize_t read_size = read(source_fd, buffer, sizeof(buffer));
        if (read_size == 0) {
            break;
        }
        if (read_size < 0) {
            ok = false;
            break;
        }
        ssize_t written = 0;
        while (written < read_size) {
            const ssize_t write_size =
                write(output_fd, buffer + written,
                      static_cast<std::size_t>(read_size - written));
            if (write_size <= 0) {
                ok = false;
                break;
            }
            written += write_size;
        }
        if (!ok) {
            break;
        }
    }

    if (ok && fchmod(output_fd, mode) != 0) {
        ok = false;
    }
    if (ok && fsync(output_fd) != 0) {
        ok = false;
    }
    close(output_fd);
    close(source_fd);
    if (!ok) {
        static_cast<void>(infra::File::Remove(output_path));
        if (reason != nullptr) {
            *reason = "copy system upgrade helper failed";
        }
        return false;
    }
    return true;
}

bool PrepareSystemUpgradeHelper(std::string* reason) {
    if (!infra::Path::MakeDirs(kSystemUpgradeRuntimePath) ||
        !infra::Path::MakeDirs(kSystemUpgradeStagePath)) {
        if (reason != nullptr) {
            *reason = "create tmpfs upgrade directory failed";
        }
        return false;
    }
    if (!upgrade_flash::IsPathOnTmpfs(kSystemUpgradeRuntimePath) ||
        !upgrade_flash::IsPathOnTmpfs(kSystemUpgradeStagePath)) {
        if (reason != nullptr) {
            *reason = "system upgrade directory is not tmpfs";
        }
        return false;
    }
    return CopyFileNoSymlink(kSystemUpgradeHelperSourcePath,
                             kSystemUpgradeHelperPath, 0755, reason);
}

bool StartSystemUpgradeHelper(const std::string& package_path,
                              bool reboot,
                              std::string* reason) {
    if (!PrepareSystemUpgradeHelper(reason)) {
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        if (reason != nullptr) {
            *reason = "fork system upgrade helper failed";
        }
        return false;
    }
    if (pid == 0) {
        if (reboot) {
            execl(kSystemUpgradeHelperPath, kSystemUpgradeHelperPath,
                  "--package", package_path.c_str(), "--stage",
                  kSystemUpgradeStagePath, "--reboot",
                  static_cast<char*>(nullptr));
        } else {
            execl(kSystemUpgradeHelperPath, kSystemUpgradeHelperPath,
                  "--package", package_path.c_str(), "--stage",
                  kSystemUpgradeStagePath, static_cast<char*>(nullptr));
        }
        _exit(127);
    }
    AppendUpgradeLog("system upgrade helper started");
    return true;
}

class UpgradePlatform : public IUpgradePlatform {
public:
    UpgradePackageInfo ValidatePackage(const std::string& package_path) override {
        last_error_.clear();
        AppendUpgradeLog("validate begin: package=" + package_path +
                         " size=" +
                         std::to_string(infra::File::Size(package_path)));
        UpgradeManifest manifest;
        std::string reason;
        if (!ReadUpgradePackageManifest(package_path, &manifest, &reason)) {
            last_error_ = reason;
            AppendUpgradeLog("validate failed: msg=" + reason);
            return UpgradePackageInfo();
        }

        ParsedUpgradePackage parsed;
        if (!ParseUpgradePackage(package_path, &parsed, &reason)) {
            last_error_ = reason;
            AppendUpgradeLog("validate failed: msg=" + reason);
            return UpgradePackageInfo();
        }
        cached_package_ = parsed;
        AppendUpgradeLog("validate ok: package=" + package_path +
                         " digest=" + parsed.sha256 + " " +
                         UpgradeManifestSummary(parsed.manifest));
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
            AppendUpgradeLog("prepare failed: msg=" + reason);
            WriteUpgradeInfo("failed", 100, false, info.version, reason);
            return false;
        }
        if (!infra::Path::MakeDirs(kUpgradeRootPath) ||
            !infra::Path::MakeDirs(kUpgradeStagePath) ||
            !infra::Path::MakeDirs(kUpgradeUploadPath)) {
            last_error_ = "create upgrade directory failed";
            AppendUpgradeLog("prepare failed: msg=create upgrade directory failed");
            return false;
        }
        if (!upgrade_flash::IsPathOnTmpfs(kUpgradeRootPath) ||
            !upgrade_flash::IsPathOnTmpfs(kUpgradeStagePath) ||
            !upgrade_flash::IsPathOnTmpfs(kUpgradeUploadPath)) {
            last_error_ = "upgrade workspace must be tmpfs";
            AppendUpgradeLog("prepare failed: msg=upgrade workspace must be tmpfs");
            return false;
        }
        if (PackageNeedsSystemUpgradeHelper(parsed.manifest) &&
            !PrepareSystemUpgradeHelper(&reason)) {
            last_error_ = reason;
            AppendUpgradeLog("prepare failed: msg=" + reason);
            WriteUpgradeInfo("failed", 100, false, info.version, reason);
            return false;
        }
        cancel_requested_ = false;
        cached_package_ = parsed;
        stage_dir_ = infra::Path::Join(
            kUpgradeStagePath,
            std::to_string(ReadSystemTimeMs()) + "-" + parsed.manifest.version);
        if (!infra::Path::MakeDirs(stage_dir_)) {
            last_error_ = "create stage directory failed";
            AppendUpgradeLog("prepare failed: msg=create stage directory failed stage=" +
                             stage_dir_);
            return false;
        }
        AppendUpgradeLog("prepare ok: stage=" + stage_dir_ + " " +
                         UpgradeManifestSummary(cached_package_.manifest));
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
        if (PackageNeedsSystemUpgradeHelper(cached_package_.manifest)) {
            AppendUpgradeLog("helper handoff begin: package=" + package_path +
                             " reboot=" +
                             (cached_package_.requires_reboot ? "true" : "false") +
                             " " +
                             UpgradeManifestSummary(cached_package_.manifest));
            if (!StartSystemUpgradeHelper(package_path,
                                          cached_package_.requires_reboot,
                                          &reason)) {
                last_error_ = reason;
                AppendUpgradeLog("helper start failed: msg=" + reason);
                WriteUpgradeInfo("failed", 100, false,
                                 cached_package_.manifest.version, reason);
                return false;
            }
            if (progress_callback) {
                progress_callback(100);
            }
            return true;
        }

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
            AppendUpgradeLog("extract failed: stage=" + stage_dir_ +
                             " msg=" + reason + " " +
                             UpgradeManifestSummary(cached_package_.manifest));
            return false;
        }
        AppendUpgradeLog("extract ok: stage=" + stage_dir_ + " " +
                         UpgradeManifestSummary(cached_package_.manifest));
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
            AppendUpgradeLog("write failed: msg=" + reason);
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
        AppendUpgradeLog(UpgradePackageIsWebOnly(cached_package_.manifest)
                             ? "web upgrade completed: " + info.version
                             : "system upgrade handed to helper: " +
                                   info.version);
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
