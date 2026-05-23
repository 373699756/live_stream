#include "device_platforms.h"

#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/hash.h"
#include "linux_platform_common.h"
#include "upgrade_package.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mtd/mtd-abi.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::ReadFirstText;
using linux_platform::ReadSystemTimeMs;
using linux_platform::Trim;

constexpr const char* kUpgradeRootPath = "/data/upgrade";
constexpr const char* kUpgradeUploadPath = "/tmp/live_stream/upgrade/uploads";
constexpr const char* kUpgradeStagePath = "/data/upgrade/staged";
constexpr const char* kUpgradeLogPath = "/data/upgrade.log";
constexpr const char* kUpgradeStatusPath = "/data/upgrade_status.json";

struct ProcMtdEntry {
    std::string device_name;
    std::string name;
    uint32_t size_bytes = 0;
    uint32_t erase_size = 0;
};

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

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

void WriteUpgradeStatus(const std::string& state,
                        uint32_t progress,
                        bool ok,
                        const std::string& version,
                        const std::string& error_message) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    std::ostringstream out;
    out << "{\n"
        << "  \"state\": \"" << JsonEscape(state) << "\",\n"
        << "  \"progress_percent\": " << progress << ",\n"
        << "  \"ok\": " << (ok ? "true" : "false") << ",\n"
        << "  \"version\": \"" << JsonEscape(version) << "\",\n"
        << "  \"error_message\": \"" << JsonEscape(error_message) << "\"\n"
        << "}\n";
    static_cast<void>(infra::File::WriteAll(kUpgradeStatusPath, out.str()));
}

bool ParseProcMtdLine(const std::string& line, ProcMtdEntry* entry) {
    if (entry == nullptr || line.compare(0, 3, "mtd") != 0) {
        return false;
    }
    const std::size_t colon = line.find(':');
    const std::size_t quote_begin = line.find('"');
    const std::size_t quote_end = line.find('"', quote_begin + 1);
    if (colon == std::string::npos || quote_begin == std::string::npos ||
        quote_end == std::string::npos) {
        return false;
    }
    std::istringstream stream(line.substr(colon + 1, quote_begin - colon - 1));
    uint32_t size = 0;
    uint32_t erase_size = 0;
    stream >> std::hex >> size >> erase_size;
    if (!stream) {
        return false;
    }
    entry->device_name = line.substr(0, colon);
    entry->name = line.substr(quote_begin + 1, quote_end - quote_begin - 1);
    entry->size_bytes = size;
    entry->erase_size = erase_size;
    return true;
}

bool ReadProcMtd(const std::string& partition, ProcMtdEntry* entry) {
    if (entry == nullptr) {
        return false;
    }
    std::istringstream stream(infra::File::ReadAll("/proc/mtd"));
    std::string line;
    while (std::getline(stream, line)) {
        ProcMtdEntry parsed;
        if (ParseProcMtdLine(Trim(line), &parsed) &&
            parsed.name == partition) {
            *entry = parsed;
            return true;
        }
    }
    return false;
}

bool ValidateMtdDevice(const UpgradePartition& partition,
                       int fd,
                       mtd_info_user* info,
                       std::string* reason) {
    if (info == nullptr || ioctl(fd, MEMGETINFO, info) != 0) {
        if (reason != nullptr) {
            *reason = "MEMGETINFO failed";
        }
        return false;
    }
    if (info->type != MTD_NORFLASH || info->erasesize == 0) {
        if (reason != nullptr) {
            *reason = "MTD device is not writable NOR flash";
        }
        return false;
    }
    ProcMtdEntry proc_entry;
    if (!ReadProcMtd(partition.name, &proc_entry) ||
        "/dev/" + proc_entry.device_name != partition.mtd_path ||
        proc_entry.size_bytes != partition.size_bytes ||
        info->size != partition.size_bytes) {
        if (reason != nullptr) {
            *reason = "MTD partition layout mismatch";
        }
        return false;
    }
    return true;
}

bool IsMounted(const std::string& mount_point) {
    if (mount_point.empty()) {
        return false;
    }
    std::istringstream stream(infra::File::ReadAll("/proc/mounts"));
    std::string dev;
    std::string dir;
    std::string rest;
    while (stream >> dev >> dir) {
        std::getline(stream, rest);
        if (dir == mount_point) {
            return true;
        }
    }
    return false;
}

bool UnmountIfMounted(const std::string& mount_point, std::string* reason) {
    if (!IsMounted(mount_point)) {
        return true;
    }
    if (umount(mount_point.c_str()) != 0) {
        if (reason != nullptr) {
            *reason = "unmount failed";
        }
        return false;
    }
    return true;
}

bool Remount(const UpgradePartition& partition, std::string* reason) {
    if (partition.block_path.empty() || partition.mount_point.empty() ||
        partition.fs_type.empty()) {
        return true;
    }
    if (mount(partition.block_path.c_str(), partition.mount_point.c_str(),
              partition.fs_type.c_str(), MS_RDONLY,
              partition.mount_options.empty() ? nullptr
                                              : partition.mount_options.c_str()) !=
        0) {
        if (reason != nullptr) {
            *reason = "remount failed";
        }
        return false;
    }
    return true;
}

bool WriteAllBytes(int fd, const std::string& image_path, uint64_t* written) {
    if (written == nullptr) {
        return false;
    }
    std::FILE* file = std::fopen(image_path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t buffer[64 * 1024];
    *written = 0;
    bool ok = true;
    while (ok) {
        const std::size_t read_size = std::fread(buffer, 1, sizeof(buffer), file);
        if (read_size == 0) {
            if (std::ferror(file) != 0) {
                ok = false;
            }
            break;
        }
        std::size_t offset = 0;
        while (offset < read_size) {
            const ssize_t write_size =
                write(fd, buffer + offset, read_size - offset);
            if (write_size <= 0) {
                ok = false;
                break;
            }
            offset += static_cast<std::size_t>(write_size);
            *written += static_cast<uint64_t>(write_size);
        }
    }
    std::fclose(file);
    return ok;
}

bool ReadBackSha256(int fd, uint64_t size, std::string* sha256) {
    if (sha256 == nullptr || lseek(fd, 0, SEEK_SET) < 0) {
        return false;
    }
    infra::Sha256 sha;
    uint8_t buffer[64 * 1024];
    uint64_t remaining = size;
    while (remaining > 0) {
        const std::size_t chunk =
            remaining < sizeof(buffer) ? static_cast<std::size_t>(remaining)
                                       : sizeof(buffer);
        const ssize_t read_size = read(fd, buffer, chunk);
        if (read_size <= 0) {
            return false;
        }
        sha.Update(buffer, static_cast<std::size_t>(read_size));
        remaining -= static_cast<uint64_t>(read_size);
    }
    *sha256 = sha.FinishHex();
    return true;
}

bool WriteMtdImage(const UpgradeCommand& command,
                   const std::string& image_path,
                   std::string* reason) {
    const UpgradePartition& partition = command.partition_info;
    const int fd = open(partition.mtd_path.c_str(), O_RDWR);
    if (fd < 0) {
        if (reason != nullptr) {
            *reason = "open MTD failed";
        }
        return false;
    }

    mtd_info_user info {};
    bool ok = ValidateMtdDevice(partition, fd, &info, reason);
    if (ok) {
        for (uint32_t offset = 0; offset < info.size; offset += info.erasesize) {
            erase_info_user erase {};
            erase.start = offset;
            erase.length = info.erasesize;
            if (ioctl(fd, MEMERASE, &erase) != 0) {
                if (reason != nullptr) {
                    *reason = "MEMERASE failed";
                }
                ok = false;
                break;
            }
        }
    }
    uint64_t written = 0;
    if (ok && lseek(fd, 0, SEEK_SET) < 0) {
        if (reason != nullptr) {
            *reason = "seek MTD failed";
        }
        ok = false;
    }
    if (ok && !WriteAllBytes(fd, image_path, &written)) {
        if (reason != nullptr) {
            *reason = "write MTD failed";
        }
        ok = false;
    }
    if (ok && written != command.size_bytes) {
        if (reason != nullptr) {
            *reason = "written size mismatch";
        }
        ok = false;
    }
    if (ok && fsync(fd) != 0) {
        if (reason != nullptr) {
            *reason = "fsync MTD failed";
        }
        ok = false;
    }
    std::string readback_sha256;
    if (ok && (!ReadBackSha256(fd, written, &readback_sha256) ||
               readback_sha256 != command.sha256)) {
        if (reason != nullptr) {
            *reason = "MTD readback sha256 mismatch";
        }
        ok = false;
    }
    close(fd);
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
        if (!UnmountIfMounted(command.partition_info.mount_point, reason)) {
            return false;
        }
        if (!WriteMtdImage(command, image_path, reason)) {
            static_cast<void>(Remount(command.partition_info, nullptr));
            return false;
        }
        if (!Remount(command.partition_info, reason)) {
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
        if (!UpgradePackageIsWebOnly(parsed.manifest)) {
            reason = "only web upgrade is supported before pending apply";
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
        stage_dir_ = infra::Path::Join(
            kUpgradeStagePath,
            std::to_string(ReadSystemTimeMs()) + "-" + parsed.manifest.version);
        return infra::Path::MakeDirs(stage_dir_);
    }

    bool WriteUpgrade(const std::string& package_path,
                      UpgradeProgressCallback progress_callback) override {
        if (package_path.empty() || stage_dir_.empty()) {
            return false;
        }
        if (cancel_requested_) {
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
        sync();
        return reboot(RB_AUTOBOOT) == 0;
    }

    bool CleanupFailedUpgrade() override {
        AppendUpgradeLog("upgrade cleanup after failure");
        return true;
    }

private:
    bool cancel_requested_ = false;
    ParsedUpgradePackage cached_package_;
    std::string stage_dir_;
};

}  // namespace

std::unique_ptr<IUpgradePlatform> CreateUpgradePlatform() {
    return std::unique_ptr<IUpgradePlatform>(new UpgradePlatform());
}

}  // namespace live_stream
