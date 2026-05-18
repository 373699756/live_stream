#include "device_platforms.h"

#include "infra/fs.h"
#include "linux_platform_common.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::IsExecutable;
using linux_platform::ReadFirstText;
using linux_platform::ReadSystemTimeMs;
using linux_platform::RunCommand;

constexpr const char *kUpgradeRootPath = "/tmp/live_stream/upgrade";
constexpr const char *kUpgradeUploadPath = "/tmp/live_stream/upgrade/uploads";
constexpr const char *kUpgradeStagePath = "/tmp/live_stream/upgrade/staged";

std::string DeviceModelString() {
    std::string model = ReadFirstText({
        "/proc/device-tree/model",
        "/sys/firmware/devicetree/base/model",
    });
    if (model.empty()) {
        model = "live_stream_ipc";
    }
    return model;
}

std::string FirmwareVersionString() {
    std::string version = ReadFirstText({
        "/etc/firmware_version",
        "/etc/version",
    });
    if (version.empty()) {
        version = "0.1.0";
    }
    return version;
}

bool IsSafeVersionChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' ||
           c == '_' || c == '-';
}

std::string FileStem(const std::string &path) {
    const std::string base = infra::Path::BaseName(path);
    const std::string::size_type dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return base;
    }
    return base.substr(0, dot);
}

std::string NormalizeVersionToken(const std::string &token) {
    if (token.empty()) {
        return std::string();
    }
    std::size_t begin = 0;
    if ((token[0] == 'v' || token[0] == 'V') && token.size() > 1) {
        begin = 1;
    }
    bool has_digit = false;
    for (std::size_t i = begin; i < token.size(); ++i) {
        const char c = token[i];
        if (!IsSafeVersionChar(c)) {
            return std::string();
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            has_digit = true;
        }
    }
    if (!has_digit) {
        return std::string();
    }
    return token.substr(begin);
}

std::string InferVersionFromPath(const std::string &path) {
    const std::string stem = FileStem(path);
    std::string token;
    std::string fallback;
    for (char c : stem) {
        if (c == '-' || c == '_' || c == ' ') {
            const std::string normalized = NormalizeVersionToken(token);
            if (!normalized.empty()) {
                fallback = normalized;
            }
            token.clear();
            continue;
        }
        token.push_back(c);
    }
    const std::string normalized = NormalizeVersionToken(token);
    if (!normalized.empty()) {
        fallback = normalized;
    }
    if (!fallback.empty()) {
        return fallback;
    }
    return stem.empty() ? "unknown" : stem;
}

std::vector<std::string> SplitVersion(const std::string &value) {
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

bool IsDigitsOnly(const std::string &value) {
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

int CompareNumericStrings(const std::string &lhs, const std::string &rhs) {
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

int CompareVersionStrings(const std::string &lhs, const std::string &rhs) {
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

char HexDigit(uint8_t value) {
    return static_cast<char>(value < 10 ? ('0' + value) : ('a' + value - 10));
}

std::string HexFromUint64(uint64_t value) {
    std::string hex(16, '0');
    for (int i = 15; i >= 0; --i) {
        hex[static_cast<std::size_t>(i)] = HexDigit(value & 0x0fU);
        value >>= 4;
    }
    return hex;
}

std::string ComputeFileDigest(const std::string &path) {
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::string();
    }
    uint64_t hash = 1469598103934665603ULL;
    uint8_t buffer[4096];
    while (true) {
        const std::size_t read_size = std::fread(buffer, 1, sizeof(buffer), file);
        if (read_size == 0) {
            break;
        }
        for (std::size_t i = 0; i < read_size; ++i) {
            hash ^= buffer[i];
            hash *= 1099511628211ULL;
        }
        if (read_size < sizeof(buffer)) {
            break;
        }
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok ? HexFromUint64(hash) : std::string();
}

int64_t FileMtimeMs(const std::string &path) {
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) != 0) {
        return 0;
    }
    return static_cast<int64_t>(file_stat.st_mtime) * 1000LL;
}

bool CopyFileWithProgress(const std::string &source_path,
                          const std::string &target_path,
                          bool *cancel_requested,
                          UpgradeProgressCallback progress_callback) {
    std::FILE *source = std::fopen(source_path.c_str(), "rb");
    if (source == nullptr) {
        return false;
    }
    std::FILE *target = std::fopen(target_path.c_str(), "wb");
    if (target == nullptr) {
        std::fclose(source);
        return false;
    }
    const uint64_t total_size = infra::File::Size(source_path);
    uint64_t copied_size = 0;
    uint8_t buffer[64 * 1024];
    bool ok = true;
    while (ok) {
        if (cancel_requested != nullptr && *cancel_requested) {
            ok = false;
            break;
        }
        const std::size_t read_size = std::fread(buffer, 1, sizeof(buffer), source);
        if (read_size == 0) {
            if (std::ferror(source) != 0) {
                ok = false;
            }
            break;
        }
        if (std::fwrite(buffer, 1, read_size, target) != read_size) {
            ok = false;
            break;
        }
        copied_size += static_cast<uint64_t>(read_size);
        if (progress_callback) {
            uint32_t progress = 100;
            if (total_size > 0) {
                progress = static_cast<uint32_t>((copied_size * 100ULL) / total_size);
            }
            progress_callback(progress > 100U ? 100U : progress);
        }
    }
    if (std::fflush(target) != 0) {
        ok = false;
    }
    std::fclose(source);
    if (std::fclose(target) != 0) {
        ok = false;
    }
    if (!ok) {
        static_cast<void>(infra::File::Remove(target_path));
        return false;
    }
    return true;
}

std::string FindUpgradeCommand() {
    static const char *const kPaths[] = {
        "/usr/sbin/sysupgrade",
        "/sbin/sysupgrade",
        "/usr/bin/sysupgrade",
        "/bin/sysupgrade",
        "/usr/bin/firmware_upgrade.sh",
        "/usr/sbin/firmware_upgrade.sh",
        "/usr/bin/upgrade.sh",
        "/usr/sbin/upgrade.sh",
        "/etc/init.d/upgrade",
    };
    for (const char *path : kPaths) {
        if (IsExecutable(path)) {
            return path;
        }
    }
    return std::string();
}

std::vector<std::string> BuildUpgradeCommand(const std::string &command_path,
                                             const std::string &package_path) {
    const std::string base_name = infra::Path::BaseName(command_path);
    if (base_name == "sysupgrade") {
        return {command_path, package_path};
    }
    return {command_path, package_path};
}

class LinuxUpgradePlatform : public IUpgradePlatform {
public:
    UpgradePackageInfo ValidatePackage(const std::string &package_path) override {
        if (!infra::File::Exists(package_path)) {
            return UpgradePackageInfo();
        }

        UpgradePackageInfo info;
        info.package_path = package_path;
        info.version = InferVersionFromPath(package_path);
        info.size_bytes = infra::File::Size(package_path);
        info.digest = ComputeFileDigest(package_path);
        info.build_time_ms = FileMtimeMs(package_path);
        info.target_model = DeviceModelString();
        info.requires_reboot = true;
        if (info.version.empty() || info.size_bytes == 0 || info.digest.empty()) {
            return UpgradePackageInfo();
        }
        return info;
    }

    std::string GetCurrentVersion() override { return FirmwareVersionString(); }

    int CompareVersion(const std::string &lhs,
                       const std::string &rhs) override {
        return CompareVersionStrings(lhs, rhs);
    }

    bool PrepareUpgrade(const UpgradePackageInfo &info) override {
        if (info.package_path.empty() || !infra::File::Exists(info.package_path)) {
            return false;
        }
        if (!infra::Path::MakeDirs(kUpgradeRootPath) ||
            !infra::Path::MakeDirs(kUpgradeStagePath) ||
            !infra::Path::MakeDirs(kUpgradeUploadPath)) {
            return false;
        }
        cancel_requested_ = false;
        staged_package_path_ = infra::Path::Join(
            kUpgradeStagePath,
            std::to_string(ReadSystemTimeMs()) + "-" +
                infra::Path::BaseName(info.package_path));
        upgrade_command_ = FindUpgradeCommand();
        pending_package_info_ = info;
        return !staged_package_path_.empty();
    }

    bool WriteUpgrade(const std::string &package_path,
                      UpgradeProgressCallback progress_callback) override {
        if (package_path.empty() || staged_package_path_.empty()) {
            return false;
        }
        return CopyFileWithProgress(package_path, staged_package_path_,
                                    &cancel_requested_, progress_callback);
    }

    bool CommitUpgrade(const UpgradePackageInfo &info) override {
        if (staged_package_path_.empty() ||
            !infra::File::Exists(staged_package_path_)) {
            return false;
        }
        if (info.version.empty() || upgrade_command_.empty()) {
            return false;
        }
        committed_package_path_ = staged_package_path_;
        pending_package_info_ = info;
        return true;
    }

    bool CancelUpgrade() override {
        cancel_requested_ = true;
        if (!staged_package_path_.empty()) {
            static_cast<void>(infra::File::Remove(staged_package_path_));
        }
        staged_package_path_.clear();
        committed_package_path_.clear();
        pending_package_info_ = UpgradePackageInfo();
        return true;
    }

    bool RebootToApply() override {
        if (upgrade_command_.empty() || committed_package_path_.empty() ||
            !infra::File::Exists(committed_package_path_)) {
            return false;
        }
        sync();
        const std::vector<std::string> command =
            BuildUpgradeCommand(upgrade_command_, committed_package_path_);
        return !command.empty() && RunCommand(command) == 0;
    }

    bool CleanupFailedUpgrade() override {
        if (!staged_package_path_.empty()) {
            static_cast<void>(infra::File::Remove(staged_package_path_));
        }
        if (!committed_package_path_.empty() &&
            committed_package_path_ != staged_package_path_) {
            static_cast<void>(infra::File::Remove(committed_package_path_));
        }
        staged_package_path_.clear();
        committed_package_path_.clear();
        pending_package_info_ = UpgradePackageInfo();
        return true;
    }

private:
    bool cancel_requested_ = false;
    std::string staged_package_path_;
    std::string committed_package_path_;
    std::string upgrade_command_;
    UpgradePackageInfo pending_package_info_;
};

}  // namespace

std::unique_ptr<IUpgradePlatform> CreateLinuxUpgradePlatform() {
    return std::unique_ptr<IUpgradePlatform>(new LinuxUpgradePlatform());
}

}  // namespace live_stream
