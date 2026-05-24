#ifndef LIVE_STREAM_APP_UPGRADE_PACKAGE_H_
#define LIVE_STREAM_APP_UPGRADE_PACKAGE_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace live_stream {

struct UpgradePartition {
    std::string name;
    std::string mtd_path;
    std::string block_path;
    std::string mount_point;
    std::string fs_type;
    std::string mount_options;
    uint32_t size_bytes = 0;
    uint32_t erase_size_bytes = 0;
    bool online_writable = false;
};

struct UpgradeCommand {
    std::string partition;
    std::string file;
    std::string sha256;
    uint64_t size_bytes = 0;
    UpgradePartition partition_info;
};

struct UpgradeManifest {
    std::string version;
    std::string board;
    std::string flash;
    std::string package_type;
    bool reboot = true;
    std::vector<UpgradeCommand> commands;
};

struct ParsedUpgradePackage {
    std::string package_path;
    uint64_t size_bytes = 0;
    int64_t mtime_ms = 0;
    std::string sha256;
    UpgradeManifest manifest;
    bool requires_reboot = true;
};

using UpgradePackageProgress = std::function<void(uint32_t progress_percent)>;

const UpgradePartition* FindUpgradePartition(const std::string& partition);
bool ParseUpgradePackage(const std::string& package_path,
                         ParsedUpgradePackage* package,
                         std::string* reason);
bool ExtractUpgradeFile(const std::string& package_path,
                        const std::string& file_name,
                        const std::string& output_path,
                        std::string* reason);
bool ExtractUpgradeFiles(const std::string& package_path,
                         const UpgradeManifest& manifest,
                         const std::string& output_dir,
                         UpgradePackageProgress progress_callback,
                         std::string* reason);
bool UpgradePackageIsWebOnly(const UpgradeManifest& manifest);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_UPGRADE_PACKAGE_H_
