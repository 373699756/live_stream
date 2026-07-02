#include "platform/linux/device_platforms.h"

#include "json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "platform/linux/linux_clock.h"
#include "platform/linux/upgrade_status_file.h"
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

using linux_platform::ReadSystemTimeMs;
using linux_platform::AppendUpgradeLogLine;

constexpr const char* kUpgradeRootPath = "/tmp/live_stream/upgrade";
constexpr const char* kUpgradeUploadPath = "/tmp/live_stream/upgrade/uploads";
constexpr const char* kUpgradeStagePath = "/tmp/live_stream/upgrade/staged";
constexpr const char* kSystemUpgradeRuntimePath = "/tmp/live_stream/upgrade";
constexpr const char* kSystemUpgradeStagePath =
    "/tmp/live_stream/upgrade/staged";
constexpr const char* kSysupgradeToolSourcePath =
    "/opt/app/sbin/live_sysupgrade";
constexpr const char* kSysupgradeToolPath =
    "/tmp/live_stream/upgrade/live_sysupgrade";
constexpr uint16_t kSysupgradeStatusPort = 80;

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

void WriteUpgradeInfo(const std::string& state,
                      uint32_t progress,
                      bool ok,
                      const std::string& version,
                      const std::string& current_stage,
                      const std::string& error_message) {
    Json root = Json::object();
    root["state"] = state;
    root["progress_percent"] = progress;
    root["current_stage"] = current_stage.empty() ? state : current_stage;
    root["ok"] = ok;
    root["version"] = version;
    root["error_message"] = error_message;
    static_cast<void>(linux_platform::WriteUpgradeStatusFile(root));
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
                     std::string* msg) {
    for (const UpgradeCommand& command : manifest.commands) {
        if (command.partition != "web") {
            if (msg != nullptr) {
                *msg = "only web upgrade is supported online";
            }
            return false;
        }
    }
    for (std::size_t i = 0; i < manifest.commands.size(); ++i) {
        const UpgradeCommand& command = manifest.commands[i];
        const std::string image_path = infra::Path::Join(stage_dir, command.file);
        AppendUpgradeLogLine("web write begin: partition=" + command.partition +
                         " file=" + command.file +
                         " image=" + image_path +
                         " size=" + std::to_string(command.size_bytes) +
                         " mtd=" + command.partition_info.mtd_path +
                         " mount=" + command.partition_info.mount_point);
        if (!upgrade_flash::UnmountIfMounted(command.partition_info.mount_point,
                                             msg)) {
            AppendUpgradeLogLine("web unmount failed: mount=" +
                             command.partition_info.mount_point +
                             " msg=" +
                             (msg == nullptr ? std::string() : *msg));
            return false;
        }
        if (!upgrade_flash::WriteMtdImage(
                command, image_path,
                [&manifest, &progress_callback, i](
                    uint32_t mtd_progress, const std::string&) {
                    if (!progress_callback || manifest.commands.empty()) {
                        return;
                    }
                    const uint32_t bounded =
                        infra::Clamp<uint32_t>(mtd_progress, 0U, 100U);
                    const uint32_t begin_progress = static_cast<uint32_t>(
                        (i * 100ULL) / manifest.commands.size());
                    const uint32_t end_progress = static_cast<uint32_t>(
                        ((i + 1) * 100ULL) / manifest.commands.size());
                    const uint32_t range =
                        end_progress > begin_progress
                            ? end_progress - begin_progress
                            : 0U;
                    progress_callback(infra::Clamp<uint32_t>(
                        begin_progress +
                            static_cast<uint32_t>((bounded * range) / 100U),
                        0U, 100U));
                },
                msg)) {
            static_cast<void>(upgrade_flash::Remount(command.partition_info,
                                                     nullptr));
            AppendUpgradeLogLine("web mtd write failed: partition=" +
                             command.partition + " mtd=" +
                             command.partition_info.mtd_path + " msg=" +
                             (msg == nullptr ? std::string() : *msg));
            return false;
        }
        if (!upgrade_flash::Remount(command.partition_info, msg)) {
            AppendUpgradeLogLine("web remount failed: mount=" +
                             command.partition_info.mount_point +
                             " msg=" +
                             (msg == nullptr ? std::string() : *msg));
            return false;
        }
        AppendUpgradeLogLine("web write done: partition=" + command.partition +
                         " file=" + command.file);
        if (progress_callback) {
            const uint32_t progress = static_cast<uint32_t>(
                ((i + 1) * 100ULL) / manifest.commands.size());
            progress_callback(infra::Clamp<uint32_t>(progress, 0U, 100U));
        }
    }
    return true;
}

bool PackageNeedsSysupgradeTool(const UpgradeManifest& manifest) {
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

std::string UpgradePartitionsText(const UpgradeManifest& manifest) {
    std::string text;
    for (const UpgradeCommand& command : manifest.commands) {
        if (!text.empty()) {
            text += ",";
        }
        text += command.partition;
    }
    return text.empty() ? "-" : text;
}

bool CopyFileNoSymlink(const std::string& source_path,
                       const std::string& output_path,
                       mode_t mode,
                       std::string* msg) {
    struct stat source_stat;
    if (lstat(source_path.c_str(), &source_stat) != 0 ||
        S_ISLNK(source_stat.st_mode) || !S_ISREG(source_stat.st_mode)) {
        if (msg != nullptr) {
            *msg = "sysupgrade tool is not a regular file";
        }
        return false;
    }

    const int source_fd = open(source_path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (source_fd < 0) {
        if (msg != nullptr) {
            *msg = "open sysupgrade tool failed";
        }
        return false;
    }

    const int output_fd = open(output_path.c_str(),
                               O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
                               mode);
    if (output_fd < 0) {
        close(source_fd);
        if (msg != nullptr) {
            *msg = "create tmpfs sysupgrade tool failed";
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
        if (msg != nullptr) {
            *msg = "copy sysupgrade tool failed";
        }
        return false;
    }
    return true;
}

bool PrepareSysupgradeToolFromCurrentSystem(std::string* msg) {
    AppendUpgradeLogLine("sysupgrade tool source: current system " +
                     std::string(kSysupgradeToolSourcePath));
    return CopyFileNoSymlink(kSysupgradeToolSourcePath, kSysupgradeToolPath,
                             0755, msg);
}

bool PrepareSysupgradeToolFromPackage(const std::string& package_path,
                                      const UpgradeManifest& manifest,
                                      std::string* msg) {
    if (manifest.sysupgrade_tool.empty()) {
        return false;
    }
    AppendUpgradeLogLine("sysupgrade tool source: package file=" +
                     manifest.sysupgrade_tool);
    if (!ExtractUpgradeSupportFile(package_path, manifest.sysupgrade_tool,
                                   kSysupgradeToolPath, msg)) {
        return false;
    }
    if (chmod(kSysupgradeToolPath, 0755) != 0) {
        if (msg != nullptr) {
            *msg = "chmod sysupgrade tool failed";
        }
        static_cast<void>(infra::File::Remove(kSysupgradeToolPath));
        return false;
    }
    return true;
}

bool PrepareSysupgradeTool(const std::string& package_path,
                           const UpgradeManifest& manifest,
                           std::string* msg) {
    if (!infra::Path::MakeDirs(kSystemUpgradeRuntimePath) ||
        !infra::Path::MakeDirs(kSystemUpgradeStagePath)) {
        if (msg != nullptr) {
            *msg = "create tmpfs upgrade directory failed";
        }
        return false;
    }
    if (!upgrade_flash::IsPathOnTmpfs(kSystemUpgradeRuntimePath) ||
        !upgrade_flash::IsPathOnTmpfs(kSystemUpgradeStagePath)) {
        if (msg != nullptr) {
            *msg = "system upgrade directory is not tmpfs";
        }
        return false;
    }
    if (PrepareSysupgradeToolFromPackage(package_path, manifest, msg)) {
        return true;
    }
    if (!manifest.sysupgrade_tool.empty()) {
        AppendUpgradeLogLine("sysupgrade package tool unavailable: msg=" +
                         (msg != nullptr ? *msg : std::string()));
        return false;
    }
    return PrepareSysupgradeToolFromCurrentSystem(msg);
}

bool StartSysupgradeTool(const std::string& package_path,
                         const UpgradeManifest& manifest,
                         const std::string& stage_dir,
                         uint16_t status_port,
                         bool reboot,
                         std::string* msg) {
    if (!PrepareSysupgradeTool(package_path, manifest, msg)) {
        return false;
    }
    if (stage_dir.empty() || !upgrade_flash::IsPathOnTmpfs(stage_dir)) {
        if (msg != nullptr) {
            *msg = "sysupgrade stage directory is not tmpfs";
        }
        return false;
    }
    const std::string status_port_text = std::to_string(status_port);
    const pid_t pid = fork();
    if (pid < 0) {
        if (msg != nullptr) {
            *msg = "fork sysupgrade tool failed";
        }
        return false;
    }
    if (pid == 0) {
        if (reboot) {
            execl(kSysupgradeToolPath, kSysupgradeToolPath,
                  "--package", package_path.c_str(), "--stage",
                  stage_dir.c_str(), "--status-port",
                  status_port_text.c_str(), "--reboot",
                  static_cast<char*>(nullptr));
        } else {
            execl(kSysupgradeToolPath, kSysupgradeToolPath,
                  "--package", package_path.c_str(), "--stage",
                  stage_dir.c_str(), "--status-port",
                  status_port_text.c_str(), static_cast<char*>(nullptr));
        }
        _exit(127);
    }
    AppendUpgradeLogLine("sysupgrade tool started");
    return true;
}

class UpgradePlatform : public IUpgradePlatform {
public:
    UpgradePackageInfo ValidatePackage(const std::string& package_path) override {
        const int64_t started_at_ms = ReadSystemTimeMs();
        last_error_.clear();
        AppendUpgradeLogLine("validate begin: package=" + package_path +
                         " size=" +
                         std::to_string(infra::File::Size(package_path)));
        Info("upgrade", "platform validate requested path=%s",
             package_path.c_str());
        UpgradeManifest manifest;
        std::string msg;
        if (!ReadUpgradePackageManifest(package_path, &manifest, &msg)) {
            last_error_ = msg;
            AppendUpgradeLogLine("validate failed: msg=" + msg);
            Warn("upgrade",
                 "platform validate manifest failed path=%s error=%s "
                 "elapsed_ms=%lld",
                 package_path.c_str(), msg.c_str(),
                 static_cast<long long>(ReadSystemTimeMs() - started_at_ms));
            return UpgradePackageInfo();
        }

        ParsedUpgradePackage parsed;
        if (!ParseUpgradePackage(package_path, &parsed, &msg)) {
            last_error_ = msg;
            AppendUpgradeLogLine("validate failed: msg=" + msg);
            Warn("upgrade",
                 "platform validate package failed path=%s version=%s "
                 "partitions=%s error=%s elapsed_ms=%lld",
                 package_path.c_str(), manifest.version.c_str(),
                 UpgradePartitionsText(manifest).c_str(), msg.c_str(),
                 static_cast<long long>(ReadSystemTimeMs() - started_at_ms));
            return UpgradePackageInfo();
        }
        cached_package_ = parsed;
        AppendUpgradeLogLine("validate ok: package=" + package_path +
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
        const std::string partitions = UpgradePartitionsText(parsed.manifest);
        AppendUpgradeLogLine("validate ok: version=" + info.version +
                         " partitions=" + partitions +
                         " reboot=" + (info.requires_reboot ? "1" : "0"));
        Info("upgrade",
             "platform validate ok path=%s version=%s board=%s partitions=%s "
             "size=%llu reboot=%d elapsed_ms=%lld",
             package_path.c_str(), info.version.c_str(),
             info.target_model.c_str(), partitions.c_str(),
             static_cast<unsigned long long>(info.size_bytes),
             info.requires_reboot ? 1 : 0,
             static_cast<long long>(ReadSystemTimeMs() - started_at_ms));
        return info;
    }

    std::string GetCurrentVersion() override {
        return LIVE_STREAM_RELEASE_VERSION;
    }

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
        std::string msg;
        if (!ParseUpgradePackage(info.package_path, &parsed, &msg)) {
            last_error_ = msg;
            AppendUpgradeLogLine("prepare failed: msg=" + msg);
            WriteUpgradeInfo("failed", 10, false, info.version,
                             "prepare failed", msg);
            return false;
        }
        if (!infra::Path::MakeDirs(kUpgradeRootPath) ||
            !infra::Path::MakeDirs(kUpgradeStagePath) ||
            !infra::Path::MakeDirs(kUpgradeUploadPath)) {
            last_error_ = "create upgrade directory failed";
            AppendUpgradeLogLine("prepare failed: msg=create upgrade directory failed");
            return false;
        }
        if (!upgrade_flash::IsPathOnTmpfs(kUpgradeRootPath) ||
            !upgrade_flash::IsPathOnTmpfs(kUpgradeStagePath) ||
            !upgrade_flash::IsPathOnTmpfs(kUpgradeUploadPath)) {
            last_error_ = "upgrade workspace must be tmpfs";
            AppendUpgradeLogLine(
                "prepare failed: msg=upgrade workspace must be tmpfs "
                "root=" +
                std::to_string(
                    upgrade_flash::IsPathOnTmpfs(kUpgradeRootPath) ? 1 : 0) +
                " stage=" +
                std::to_string(
                    upgrade_flash::IsPathOnTmpfs(kUpgradeStagePath) ? 1 : 0) +
                " upload=" +
                std::to_string(
                    upgrade_flash::IsPathOnTmpfs(kUpgradeUploadPath) ? 1 : 0));
            return false;
        }
        if (PackageNeedsSysupgradeTool(parsed.manifest) &&
            !PrepareSysupgradeTool(parsed.package_path, parsed.manifest,
                                   &msg)) {
            last_error_ = msg;
            AppendUpgradeLogLine("prepare failed: msg=" + msg);
            WriteUpgradeInfo("failed", 10, false, info.version,
                             "prepare failed", msg);
            return false;
        }
        auto_reboot_ = false;
        cancel_requested_ = false;
        external_flash_writer_active_ = false;
        cached_package_ = parsed;
        stage_dir_ = infra::Path::Join(
            kUpgradeStagePath,
            std::to_string(ReadSystemTimeMs()) + "-" + parsed.manifest.version);
        if (!infra::Path::MakeDirs(stage_dir_)) {
            last_error_ = "create stage directory failed";
            AppendUpgradeLogLine("prepare failed: msg=create stage directory failed stage=" +
                             stage_dir_);
            return false;
        }
        AppendUpgradeLogLine("prepare ok: stage=" + stage_dir_ + " " +
                         UpgradeManifestSummary(cached_package_.manifest));
        return true;
    }

    void SetAutoRebootPolicy(bool auto_reboot) override {
        auto_reboot_ = auto_reboot;
    }

    bool WriteUpgrade(const std::string& package_path,
                      UpgradeProgressCallback progress_callback) override {
        last_error_.clear();
        uint32_t last_platform_progress = 0;
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
        std::string msg;
        if (PackageNeedsSysupgradeTool(cached_package_.manifest)) {
            const bool tool_reboot =
                cached_package_.requires_reboot && auto_reboot_;
            AppendUpgradeLogLine(
                "sysupgrade tool handoff: package=" + package_path +
                " reboot=" +
                (tool_reboot ? "true" : "false") + " " +
                UpgradeManifestSummary(cached_package_.manifest));
            if (!StartSysupgradeTool(package_path,
                                     cached_package_.manifest,
                                     stage_dir_,
                                     kSysupgradeStatusPort,
                                     tool_reboot,
                                     &msg)) {
                last_error_ = msg;
                AppendUpgradeLogLine("sysupgrade tool start failed: msg=" + msg);
                WriteUpgradeInfo("failed", 20, false,
                                 cached_package_.manifest.version,
                                 "sysupgrade helper start failed", msg);
                return false;
            }
            if (progress_callback) {
                progress_callback(20);
            }
            external_flash_writer_active_ = true;
            WriteUpgradeInfo("writing", 20, true,
                             cached_package_.manifest.version,
                             "sysupgrade helper started; flash progress is "
                             "recorded in /data/upgrade_status.json",
                             "");
            return true;
        }

        const bool extract_ok = ExtractUpgradeFiles(
            package_path, cached_package_.manifest, stage_dir_,
            [&last_platform_progress, progress_callback](uint32_t progress) {
                last_platform_progress = progress / 2;
                if (progress_callback) {
                    progress_callback(last_platform_progress);
                }
            },
            &msg);
        if (!extract_ok) {
            last_error_ = msg;
            AppendUpgradeLogLine("extract failed: stage=" + stage_dir_ +
                             " msg=" + msg + " " +
                             UpgradeManifestSummary(cached_package_.manifest));
            return false;
        }
        AppendUpgradeLogLine("extract ok: stage=" + stage_dir_ + " " +
                         UpgradeManifestSummary(cached_package_.manifest));
        if (cancel_requested_) {
            last_error_ = "upgrade canceled";
            return false;
        }
        const bool write_ok = ApplyWebUpgrade(
            cached_package_.manifest, stage_dir_,
            [&last_platform_progress, progress_callback](uint32_t progress) {
                last_platform_progress = 50U + progress / 2;
                if (progress_callback) {
                    progress_callback(last_platform_progress);
                }
            },
            &msg);
        if (!write_ok) {
            last_error_ = msg;
            AppendUpgradeLogLine("write failed: msg=" + msg);
            WriteUpgradeInfo("failed", last_platform_progress, false,
                             cached_package_.manifest.version,
                             "write failed", msg);
        }
        return write_ok;
    }

    bool IsExternalFlashWriterActive() const override {
        return external_flash_writer_active_;
    }

    bool CommitUpgrade(const UpgradePackageInfo& info) override {
        last_error_.clear();
        if (info.version.empty()) {
            last_error_ = "upgrade version is empty";
            return false;
        }
        const bool web_only = UpgradePackageIsWebOnly(cached_package_.manifest);
        WriteUpgradeInfo(info.requires_reboot ? "waiting_reboot" : "completed",
                         web_only ? 100 : 20, true, info.version,
                         web_only
                             ? "web flash written"
                             : "sysupgrade helper owns flash writing",
                         "");
        AppendUpgradeLogLine(UpgradePackageIsWebOnly(cached_package_.manifest)
                             ? "web upgrade completed: " + info.version
                             : "system upgrade handed to sysupgrade tool: " +
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
        AppendUpgradeLogLine("upgrade cleanup after failure");
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
    bool external_flash_writer_active_ = false;
    bool auto_reboot_ = false;
    ParsedUpgradePackage cached_package_;
    std::string stage_dir_;
    std::string last_error_;
};

}  // namespace

std::unique_ptr<IUpgradePlatform> CreateUpgradePlatform() {
    return std::unique_ptr<IUpgradePlatform>(new UpgradePlatform());
}

}  // namespace live_stream
