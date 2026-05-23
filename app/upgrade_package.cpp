#include "upgrade_package.h"

#include "config_json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/hash.h"
#include "json_utils.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace live_stream {
namespace {

constexpr const char* kExpectedBoard = "Hi3516DV300";
constexpr const char* kExpectedFlash = "spi-nor-32m";
constexpr const char* kExpectedPackageType = "normal";
constexpr uint32_t kZipLocalFileHeader = 0x04034b50U;
constexpr uint16_t kZipMethodStored = 0;

const UpgradePartition kPartitions[] = {
    {"kernel", "/dev/mtd1", "", "", "", "", 0x00400000U, false},
    {"rootfs", "/dev/mtd2", "", "", "", "", 0x00c00000U, false},
    {"bin", "/dev/mtd3", "/dev/mtdblock3", "/opt/app", "squashfs", "ro",
     0x00a00000U, false},
    {"web", "/dev/mtd4", "/dev/mtdblock4", "/www", "squashfs", "ro",
     0x00200000U, true},
    {"config", "/dev/mtd5", "/dev/mtdblock5", "/config", "jffs2", "rw",
     0x00100000U, false},
};

struct ZipEntry {
    std::string name;
    uint32_t data_offset = 0;
    uint32_t size = 0;
};

uint16_t ReadLe16(const std::string& data, std::size_t offset) {
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(data.data() + offset);
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t ReadLe32(const std::string& data, std::size_t offset) {
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(data.data() + offset);
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

bool IsSafeZipPath(const std::string& path) {
    return !path.empty() && path[0] != '/' &&
           path.find('\\') == std::string::npos &&
           path.find("..") == std::string::npos;
}

bool ReadStoreOnlyZipEntries(const std::string& package_path,
                             std::string* zip_data,
                             std::vector<ZipEntry>* entries,
                             std::string* reason) {
    if (zip_data == nullptr || entries == nullptr) {
        return false;
    }
    *zip_data = infra::File::ReadAll(package_path);
    if (zip_data->empty()) {
        if (reason != nullptr) {
            *reason = "package is empty or unreadable";
        }
        return false;
    }

    std::size_t offset = 0;
    while (offset + 30 <= zip_data->size()) {
        const uint32_t signature = ReadLe32(*zip_data, offset);
        if (signature != kZipLocalFileHeader) {
            break;
        }
        const uint16_t flags = ReadLe16(*zip_data, offset + 6);
        const uint16_t method = ReadLe16(*zip_data, offset + 8);
        const uint32_t compressed_size = ReadLe32(*zip_data, offset + 18);
        const uint32_t uncompressed_size = ReadLe32(*zip_data, offset + 22);
        const uint16_t name_size = ReadLe16(*zip_data, offset + 26);
        const uint16_t extra_size = ReadLe16(*zip_data, offset + 28);
        const std::size_t name_offset = offset + 30;
        const std::size_t data_offset =
            name_offset + name_size + extra_size;
        const std::size_t next_offset =
            data_offset + static_cast<std::size_t>(compressed_size);
        if ((flags & 0x0008U) != 0 || method != kZipMethodStored ||
            compressed_size != uncompressed_size ||
            name_size == 0 || data_offset > zip_data->size() ||
            next_offset > zip_data->size()) {
            if (reason != nullptr) {
                *reason = "upgrade.zip must use stored entries";
            }
            return false;
        }
        const std::string name =
            zip_data->substr(name_offset, static_cast<std::size_t>(name_size));
        if (!IsSafeZipPath(name)) {
            if (reason != nullptr) {
                *reason = "unsafe zip entry path";
            }
            return false;
        }
        ZipEntry entry;
        entry.name = name;
        entry.data_offset = static_cast<uint32_t>(data_offset);
        entry.size = compressed_size;
        entries->push_back(entry);
        offset = next_offset;
    }
    if (entries->empty()) {
        if (reason != nullptr) {
            *reason = "upgrade.zip has no readable entries";
        }
        return false;
    }
    return true;
}

const ZipEntry* FindZipEntry(const std::vector<ZipEntry>& entries,
                             const std::string& name) {
    for (const ZipEntry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

std::string ReadZipEntryData(const std::string& zip_data,
                             const ZipEntry& entry) {
    if (entry.data_offset > zip_data.size() ||
        static_cast<std::size_t>(entry.data_offset) + entry.size >
            zip_data.size()) {
        return std::string();
    }
    return zip_data.substr(entry.data_offset, entry.size);
}

bool FileMtimeMs(const std::string& path, int64_t* mtime_ms) {
    if (mtime_ms == nullptr) {
        return false;
    }
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) != 0) {
        return false;
    }
    *mtime_ms = static_cast<int64_t>(file_stat.st_mtime) * 1000LL;
    return true;
}

bool ReadManifest(const std::string& install_text,
                  UpgradeManifest* manifest,
                  std::string* reason) {
    if (manifest == nullptr) {
        return false;
    }
    ConfigJson root = ConfigJson::parse(install_text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (reason != nullptr) {
            *reason = "Install is not valid JSON";
        }
        return false;
    }

    UpgradeManifest parsed;
    if (!json_utils::ReadField(root, "Version", &parsed.version) ||
        !json_utils::ReadField(root, "Board", &parsed.board) ||
        !json_utils::ReadField(root, "Flash", &parsed.flash) ||
        !json_utils::ReadField(root, "PackageType", &parsed.package_type) ||
        !json_utils::ReadField(root, "Reboot", &parsed.reboot) ||
        !root.contains("Commands") || !root.at("Commands").is_array()) {
        if (reason != nullptr) {
            *reason = "Install fields are incomplete";
        }
        return false;
    }
    if (parsed.version.empty() || parsed.board != kExpectedBoard ||
        parsed.flash != kExpectedFlash ||
        parsed.package_type != kExpectedPackageType) {
        if (reason != nullptr) {
            *reason = "Install target does not match this device";
        }
        return false;
    }

    for (const ConfigJson& item : root.at("Commands")) {
        if (!item.is_object()) {
            if (reason != nullptr) {
                *reason = "Install command is invalid";
            }
            return false;
        }
        std::string action;
        UpgradeCommand command;
        if (!json_utils::ReadField(item, "Action", &action) ||
            !json_utils::ReadField(item, "Partition", &command.partition) ||
            !json_utils::ReadField(item, "File", &command.file) ||
            !json_utils::ReadField(item, "Sha256", &command.sha256)) {
            if (reason != nullptr) {
                *reason = "Install command fields are incomplete";
            }
            return false;
        }
        const UpgradePartition* partition =
            FindUpgradePartition(command.partition);
        if (action != "burn" || partition == nullptr ||
            !infra::IsSha256HexString(command.sha256) ||
            !IsSafeZipPath(command.file)) {
            if (reason != nullptr) {
                *reason = "Install command is not allowed";
            }
            return false;
        }
        for (const UpgradeCommand& existing : parsed.commands) {
            if (existing.partition == command.partition ||
                existing.file == command.file) {
                if (reason != nullptr) {
                    *reason = "Install command is duplicated";
                }
                return false;
            }
        }
        command.partition_info = *partition;
        parsed.commands.push_back(command);
    }
    if (parsed.commands.empty()) {
        if (reason != nullptr) {
            *reason = "Install has no commands";
        }
        return false;
    }
    *manifest = parsed;
    return true;
}

bool ValidateCommandFiles(const std::string& zip_data,
                          const std::vector<ZipEntry>& entries,
                          UpgradeManifest* manifest,
                          std::string* reason) {
    if (manifest == nullptr) {
        return false;
    }
    for (UpgradeCommand& command : manifest->commands) {
        const ZipEntry* entry = FindZipEntry(entries, command.file);
        if (entry == nullptr) {
            if (reason != nullptr) {
                *reason = "upgrade image file is missing";
            }
            return false;
        }
        if (entry->size == 0 ||
            entry->size > command.partition_info.size_bytes) {
            if (reason != nullptr) {
                *reason = "upgrade image size exceeds partition";
            }
            return false;
        }
        const std::string data = ReadZipEntryData(zip_data, *entry);
        if (infra::Sha256Hex(data) != command.sha256) {
            if (reason != nullptr) {
                *reason = "upgrade image sha256 mismatch";
            }
            return false;
        }
        command.size_bytes = entry->size;
    }
    return true;
}

}  // namespace

const UpgradePartition* FindUpgradePartition(const std::string& partition) {
    for (const UpgradePartition& item : kPartitions) {
        if (item.name == partition) {
            return &item;
        }
    }
    return nullptr;
}

bool ParseUpgradePackage(const std::string& package_path,
                         ParsedUpgradePackage* package,
                         std::string* reason) {
    if (package == nullptr || !infra::File::Exists(package_path)) {
        if (reason != nullptr) {
            *reason = "package not found";
        }
        return false;
    }
    std::string zip_data;
    std::vector<ZipEntry> entries;
    if (!ReadStoreOnlyZipEntries(package_path, &zip_data, &entries, reason)) {
        return false;
    }
    const ZipEntry* install_entry = FindZipEntry(entries, "Install");
    if (install_entry == nullptr) {
        if (reason != nullptr) {
            *reason = "Install is missing";
        }
        return false;
    }
    UpgradeManifest manifest;
    if (!ReadManifest(ReadZipEntryData(zip_data, *install_entry), &manifest,
                      reason) ||
        !ValidateCommandFiles(zip_data, entries, &manifest, reason)) {
        return false;
    }

    ParsedUpgradePackage parsed;
    parsed.package_path = package_path;
    parsed.size_bytes = infra::File::Size(package_path);
    parsed.sha256 = infra::Sha256FileHex(package_path);
    static_cast<void>(FileMtimeMs(package_path, &parsed.mtime_ms));
    parsed.manifest = manifest;
    parsed.requires_reboot =
        manifest.reboot || !UpgradePackageIsWebOnly(manifest);
    *package = parsed;
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

bool ExtractUpgradeFile(const std::string& package_path,
                        const std::string& file_name,
                        const std::string& output_path,
                        std::string* reason) {
    std::string zip_data;
    std::vector<ZipEntry> entries;
    if (!ReadStoreOnlyZipEntries(package_path, &zip_data, &entries, reason)) {
        return false;
    }
    const ZipEntry* entry = FindZipEntry(entries, file_name);
    if (entry == nullptr) {
        if (reason != nullptr) {
            *reason = "upgrade image file is missing";
        }
        return false;
    }
    if (!infra::Path::MakeDirs(infra::Path::DirName(output_path)) ||
        !infra::File::WriteAll(output_path, ReadZipEntryData(zip_data, *entry))) {
        if (reason != nullptr) {
            *reason = "failed to extract upgrade image";
        }
        return false;
    }
    return true;
}

bool ExtractUpgradeFiles(const std::string& package_path,
                         const UpgradeManifest& manifest,
                         const std::string& output_dir,
                         UpgradePackageProgress progress_callback,
                         std::string* reason) {
    if (manifest.commands.empty() || output_dir.empty() ||
        !infra::Path::MakeDirs(output_dir)) {
        if (reason != nullptr) {
            *reason = "invalid extract target";
        }
        return false;
    }
    for (std::size_t i = 0; i < manifest.commands.size(); ++i) {
        const UpgradeCommand& command = manifest.commands[i];
        const std::string output_path =
            infra::Path::Join(output_dir, command.file);
        if (!ExtractUpgradeFile(package_path, command.file, output_path,
                                reason)) {
            return false;
        }
        if (infra::Sha256FileHex(output_path) != command.sha256) {
            if (reason != nullptr) {
                *reason = "extracted upgrade image sha256 mismatch";
            }
            return false;
        }
        if (progress_callback) {
            const uint32_t progress = static_cast<uint32_t>(
                ((i + 1) * 100ULL) / manifest.commands.size());
            progress_callback(infra::Clamp<uint32_t>(progress, 0U, 100U));
        }
    }
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

bool UpgradePackageIsWebOnly(const UpgradeManifest& manifest) {
    if (manifest.commands.empty()) {
        return false;
    }
    for (const UpgradeCommand& command : manifest.commands) {
        if (command.partition != "web") {
            return false;
        }
    }
    return true;
}

}  // namespace live_stream
