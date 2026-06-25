#include "system/package.h"

#include "json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/hash.h"
#include "json_reader.h"

#include <cctype>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

namespace live_stream {
namespace {

constexpr const char* kExpectedBoard = "Hi3516DV300";
constexpr const char* kExpectedFlash = "spi-nor-32m";
constexpr const char* kExpectedPackageType = "normal";
constexpr uint32_t kZipLocalFileHeader = 0x04034b50U;
constexpr uint16_t kZipMethodStored = 0;
constexpr const char* kManifestName = "Install";
constexpr const char* kManifestSignatureName = "Install.sig";
constexpr const char* kUpgradePublicKeyPath = "/config/upgrade_public_key.pem";
constexpr uint32_t kSpiNorEraseSize = 0x00010000U;
constexpr uint64_t kMaxUpgradePackageSize = 32ULL * 1024ULL * 1024ULL;

const UpgradePartition kPartitions[] = {
    {"kernel", "/dev/mtd1", "", "", "", "", 0x00400000U, kSpiNorEraseSize,
     false},
    {"rootfs", "/dev/mtd2", "", "", "", "", 0x00c00000U, kSpiNorEraseSize,
     false},
    {"bin", "/dev/mtd3", "/dev/mtdblock3", "/opt/app", "squashfs", "ro",
     0x00a00000U, kSpiNorEraseSize, false},
    {"web", "/dev/mtd4", "/dev/mtdblock4", "/www", "squashfs", "ro",
     0x00200000U, kSpiNorEraseSize, true},
    {"config", "/dev/mtd5", "/dev/mtdblock5", "/config", "jffs2", "rw",
     0x00100000U, kSpiNorEraseSize, false},
};

constexpr const char kBuiltInUpgradePublicKeyPem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "REPLACE_WITH_PRODUCTION_UPGRADE_PUBLIC_KEY\n"
    "-----END PUBLIC KEY-----\n";

struct ZipEntry {
    std::string name;
    uint32_t data_offset = 0;
    uint32_t size = 0;
};

struct MappedUpgradePackage {
    MappedUpgradePackage() = default;
    ~MappedUpgradePackage() { Close(); }

    MappedUpgradePackage(const MappedUpgradePackage&) = delete;
    MappedUpgradePackage& operator=(const MappedUpgradePackage&) = delete;

    bool Open(const std::string& package_path, std::string* reason) {
        Close();
        const uint64_t package_size = infra::File::Size(package_path);
        if (package_size == 0 || package_size > kMaxUpgradePackageSize) {
            if (reason != nullptr) {
                *reason = "package size is not allowed";
            }
            return false;
        }
        if (package_size > static_cast<uint64_t>(SIZE_MAX)) {
            if (reason != nullptr) {
                *reason = "package size is not supported";
            }
            return false;
        }

        fd = open(package_path.c_str(), O_RDONLY);
        if (fd < 0) {
            if (reason != nullptr) {
                *reason = "package is unreadable";
            }
            return false;
        }
        size = static_cast<std::size_t>(package_size);
        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            Close();
            if (reason != nullptr) {
                *reason = "package map failed";
            }
            return false;
        }
        data = static_cast<const uint8_t*>(mapped);
        return true;
    }

    void Close() {
        if (data != nullptr) {
            munmap(const_cast<uint8_t*>(data), size);
            data = nullptr;
        }
        size = 0;
        entries.clear();
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    int fd = -1;
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    std::vector<ZipEntry> entries;
};

uint16_t ReadLe16(const uint8_t* data, std::size_t offset) {
    const uint8_t* bytes =
        data + offset;
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t ReadLe32(const uint8_t* data, std::size_t offset) {
    const uint8_t* bytes =
        data + offset;
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

bool OpenStoreOnlyZipPackage(const std::string& package_path,
                             MappedUpgradePackage* package,
                             std::string* reason) {
    if (package == nullptr) {
        return false;
    }
    if (!package->Open(package_path, reason)) {
        return false;
    }

    std::size_t offset = 0;
    while (offset + 30 <= package->size) {
        const uint32_t signature = ReadLe32(package->data, offset);
        if (signature != kZipLocalFileHeader) {
            break;
        }
        const uint16_t flags = ReadLe16(package->data, offset + 6);
        const uint16_t method = ReadLe16(package->data, offset + 8);
        const uint32_t compressed_size = ReadLe32(package->data, offset + 18);
        const uint32_t uncompressed_size = ReadLe32(package->data, offset + 22);
        const uint16_t name_size = ReadLe16(package->data, offset + 26);
        const uint16_t extra_size = ReadLe16(package->data, offset + 28);
        const std::size_t name_offset = offset + 30;
        const std::size_t data_offset =
            name_offset + name_size + extra_size;
        const std::size_t next_offset =
            data_offset + static_cast<std::size_t>(compressed_size);
        if ((flags & 0x0008U) != 0 || method != kZipMethodStored ||
            compressed_size != uncompressed_size ||
            name_size == 0 || data_offset > package->size ||
            next_offset > package->size) {
            if (reason != nullptr) {
                *reason = "upgrade.zip must use stored entries";
            }
            return false;
        }
        const std::string name(
            reinterpret_cast<const char*>(package->data + name_offset),
            static_cast<std::size_t>(name_size));
        if (!IsSafeZipPath(name)) {
            if (reason != nullptr) {
                *reason = "unsafe zip entry path";
            }
            return false;
        }
        for (const ZipEntry& existing : package->entries) {
            if (existing.name == name) {
                if (reason != nullptr) {
                    *reason = "duplicate zip entry path";
                }
                return false;
            }
        }
        ZipEntry entry;
        entry.name = name;
        entry.data_offset = static_cast<uint32_t>(data_offset);
        entry.size = compressed_size;
        package->entries.push_back(entry);
        offset = next_offset;
    }
    if (package->entries.empty()) {
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

bool IsManifestOrSignatureEntry(const ZipEntry& entry) {
    return entry.name == kManifestName || entry.name == kManifestSignatureName;
}

std::string ReadZipEntryData(const MappedUpgradePackage& package,
                             const ZipEntry& entry) {
    if (package.data == nullptr || entry.data_offset > package.size ||
        static_cast<std::size_t>(entry.data_offset) + entry.size >
            package.size) {
        return std::string();
    }
    return std::string(
        reinterpret_cast<const char*>(package.data + entry.data_offset),
        entry.size);
}

std::string ZipEntrySha256Hex(const MappedUpgradePackage& package,
                              const ZipEntry& entry) {
    if (package.data == nullptr || entry.data_offset > package.size ||
        static_cast<std::size_t>(entry.data_offset) + entry.size >
            package.size) {
        return std::string();
    }
    infra::Sha256 sha;
    sha.Update(package.data + entry.data_offset, entry.size);
    return sha.FinishHex();
}

bool WriteZipEntryData(const MappedUpgradePackage& package,
                       const ZipEntry& entry,
                       const std::string& output_path) {
    if (package.data == nullptr || entry.data_offset > package.size ||
        static_cast<std::size_t>(entry.data_offset) + entry.size >
            package.size) {
        return false;
    }
    std::FILE* output = std::fopen(output_path.c_str(), "wb");
    if (output == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(
        package.data + entry.data_offset, 1, entry.size, output);
    const bool closed = std::fclose(output) == 0;
    return written == entry.size && closed;
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

bool IsProductionPublicKeyConfigured(const std::string& public_key_pem) {
    return public_key_pem.find("REPLACE_WITH_PRODUCTION_UPGRADE_PUBLIC_KEY") ==
           std::string::npos;
}

std::string LoadUpgradePublicKeyPem() {
    std::string public_key_pem = infra::File::ReadAll(kUpgradePublicKeyPath);
    if (IsProductionPublicKeyConfigured(public_key_pem)) {
        return public_key_pem;
    }
    public_key_pem = kBuiltInUpgradePublicKeyPem;
    if (IsProductionPublicKeyConfigured(public_key_pem)) {
        return public_key_pem;
    }
    return std::string();
}

bool VerifyInstallSignature(const std::string& install_text,
                            const std::string& signature,
                            std::string* reason) {
    if (signature.empty()) {
        if (reason != nullptr) {
            *reason = "Install signature is missing";
        }
        return false;
    }
    const std::string public_key_pem = LoadUpgradePublicKeyPem();
    if (public_key_pem.empty()) {
        if (reason != nullptr) {
            *reason = "upgrade public key is not configured";
        }
        return false;
    }

    BIO* key_bio = BIO_new_mem_buf(public_key_pem.data(),
                                   static_cast<int>(public_key_pem.size()));
    if (key_bio == nullptr) {
        if (reason != nullptr) {
            *reason = "upgrade public key load failed";
        }
        return false;
    }
    EVP_PKEY* public_key = PEM_read_bio_PUBKEY(key_bio, nullptr, nullptr,
                                               nullptr);
    BIO_free(key_bio);
    if (public_key == nullptr) {
        if (reason != nullptr) {
            *reason = "upgrade public key is invalid";
        }
        return false;
    }

    EVP_MD_CTX* verify_context = EVP_MD_CTX_new();
    bool ok = false;
    if (verify_context != nullptr &&
        EVP_DigestVerifyInit(verify_context, nullptr, EVP_sha256(), nullptr,
                             public_key) == 1 &&
        EVP_DigestVerifyUpdate(verify_context, install_text.data(),
                               install_text.size()) == 1 &&
        EVP_DigestVerifyFinal(
            verify_context,
            reinterpret_cast<const unsigned char*>(signature.data()),
            signature.size()) == 1) {
        ok = true;
    }
    EVP_MD_CTX_free(verify_context);
    EVP_PKEY_free(public_key);
    if (!ok && reason != nullptr) {
        *reason = "Install signature verification failed";
    }
    return ok;
}

bool ReadManifest(const std::string& install_text,
                  UpgradeManifest* manifest,
                  std::string* reason) {
    if (manifest == nullptr) {
        return false;
    }
    Json root = Json::parse(install_text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (reason != nullptr) {
            *reason = "Install is not valid JSON";
        }
        return false;
    }

    UpgradeManifest parsed;
    if (!json_reader::ReadField(root, "Version", &parsed.version) ||
        !json_reader::ReadField(root, "Board", &parsed.board) ||
        !json_reader::ReadField(root, "Flash", &parsed.flash) ||
        !json_reader::ReadField(root, "PackageType", &parsed.package_type) ||
        !json_reader::ReadField(root, "Reboot", &parsed.reboot) ||
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

    for (const Json& item : root.at("Commands")) {
        if (!item.is_object()) {
            if (reason != nullptr) {
                *reason = "Install command is invalid";
            }
            return false;
        }
        std::string action;
        UpgradeCommand command;
        if (!json_reader::ReadField(item, "Action", &action) ||
            !json_reader::ReadField(item, "Partition", &command.partition) ||
            !json_reader::ReadField(item, "File", &command.file) ||
            !json_reader::ReadField(item, "Sha256", &command.sha256)) {
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
        if (command.partition == "rootfs") {
            if (reason != nullptr) {
                *reason = "rootfs online upgrade is disabled";
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

bool ValidateCommandFiles(const MappedUpgradePackage& package,
                          UpgradeManifest* manifest,
                          std::string* reason) {
    if (manifest == nullptr) {
        return false;
    }
    for (UpgradeCommand& command : manifest->commands) {
        const ZipEntry* entry = FindZipEntry(package.entries, command.file);
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
        if (ZipEntrySha256Hex(package, *entry) != command.sha256) {
            if (reason != nullptr) {
                *reason = "upgrade image sha256 mismatch";
            }
            return false;
        }
        command.size_bytes = entry->size;
    }
    for (const ZipEntry& entry : package.entries) {
        if (IsManifestOrSignatureEntry(entry)) {
            continue;
        }
        bool declared = false;
        for (const UpgradeCommand& command : manifest->commands) {
            if (entry.name == command.file) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            if (reason != nullptr) {
                *reason = "upgrade package contains undeclared file";
            }
            return false;
        }
    }
    return true;
}

bool ReadVerifiedManifest(const MappedUpgradePackage& package,
                          UpgradeManifest* manifest,
                          std::string* reason) {
    if (manifest == nullptr) {
        return false;
    }
    const ZipEntry* install_entry = FindZipEntry(package.entries, kManifestName);
    if (install_entry == nullptr) {
        if (reason != nullptr) {
            *reason = "Install is missing";
        }
        return false;
    }
    const ZipEntry* signature_entry =
        FindZipEntry(package.entries, kManifestSignatureName);
    if (signature_entry == nullptr) {
        if (reason != nullptr) {
            *reason = "Install signature is missing";
        }
        return false;
    }
    const std::string install_text = ReadZipEntryData(package, *install_entry);
    if (!VerifyInstallSignature(
            install_text, ReadZipEntryData(package, *signature_entry),
            reason)) {
        return false;
    }
    if (!ReadManifest(install_text, manifest, reason) ||
        !ValidateCommandFiles(package, manifest, reason)) {
        return false;
    }
    return true;
}

bool ReadSignedManifest(const MappedUpgradePackage& package,
                        UpgradeManifest* manifest,
                        std::string* reason) {
    if (manifest == nullptr) {
        return false;
    }
    const ZipEntry* install_entry = FindZipEntry(package.entries, kManifestName);
    if (install_entry == nullptr) {
        if (reason != nullptr) {
            *reason = "Install is missing";
        }
        return false;
    }
    const ZipEntry* signature_entry =
        FindZipEntry(package.entries, kManifestSignatureName);
    if (signature_entry == nullptr) {
        if (reason != nullptr) {
            *reason = "Install signature is missing";
        }
        return false;
    }
    const std::string install_text = ReadZipEntryData(package, *install_entry);
    if (!VerifyInstallSignature(
            install_text, ReadZipEntryData(package, *signature_entry),
            reason)) {
        return false;
    }
    return ReadManifest(install_text, manifest, reason);
}

bool ManifestMatches(const UpgradeManifest& expected,
                     const UpgradeManifest& actual) {
    if (expected.version != actual.version || expected.board != actual.board ||
        expected.flash != actual.flash ||
        expected.package_type != actual.package_type ||
        expected.reboot != actual.reboot ||
        expected.commands.size() != actual.commands.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.commands.size(); ++i) {
        const UpgradeCommand& left = expected.commands[i];
        const UpgradeCommand& right = actual.commands[i];
        if (left.partition != right.partition || left.file != right.file ||
            left.sha256 != right.sha256 ||
            left.size_bytes != right.size_bytes) {
            return false;
        }
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
    MappedUpgradePackage zip_package;
    if (!OpenStoreOnlyZipPackage(package_path, &zip_package, reason)) {
        return false;
    }
    UpgradeManifest manifest;
    if (!ReadVerifiedManifest(zip_package, &manifest, reason)) {
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

bool ReadUpgradePackageManifest(const std::string& package_path,
                                UpgradeManifest* manifest,
                                std::string* reason) {
    if (manifest == nullptr || !infra::File::Exists(package_path)) {
        if (reason != nullptr) {
            *reason = "package not found";
        }
        return false;
    }
    MappedUpgradePackage zip_package;
    if (!OpenStoreOnlyZipPackage(package_path, &zip_package, reason)) {
        return false;
    }
    if (!ReadSignedManifest(zip_package, manifest, reason)) {
        return false;
    }
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

bool ExtractUpgradeFile(const std::string& package_path,
                        const std::string& file_name,
                        const std::string& output_path,
                        std::string* reason) {
    MappedUpgradePackage zip_package;
    if (!OpenStoreOnlyZipPackage(package_path, &zip_package, reason)) {
        return false;
    }
    UpgradeManifest manifest;
    if (!ReadVerifiedManifest(zip_package, &manifest, reason)) {
        return false;
    }
    bool declared = false;
    for (const UpgradeCommand& command : manifest.commands) {
        if (command.file == file_name) {
            declared = true;
            break;
        }
    }
    if (!declared) {
        if (reason != nullptr) {
            *reason = "upgrade image file is not declared";
        }
        return false;
    }
    const ZipEntry* entry = FindZipEntry(zip_package.entries, file_name);
    if (entry == nullptr) {
        if (reason != nullptr) {
            *reason = "upgrade image file is missing";
        }
        return false;
    }
    if (!infra::Path::MakeDirs(infra::Path::DirName(output_path)) ||
        !WriteZipEntryData(zip_package, *entry, output_path)) {
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
    MappedUpgradePackage zip_package;
    if (!OpenStoreOnlyZipPackage(package_path, &zip_package, reason)) {
        return false;
    }
    UpgradeManifest verified_manifest;
    if (!ReadVerifiedManifest(zip_package, &verified_manifest, reason)) {
        return false;
    }
    if (!ManifestMatches(manifest, verified_manifest)) {
        if (reason != nullptr) {
            *reason = "upgrade manifest changed before extract";
        }
        return false;
    }
    for (std::size_t i = 0; i < verified_manifest.commands.size(); ++i) {
        const UpgradeCommand& command = verified_manifest.commands[i];
        const std::string output_path =
            infra::Path::Join(output_dir, command.file);
        const ZipEntry* entry = FindZipEntry(zip_package.entries, command.file);
        if (entry == nullptr) {
            if (reason != nullptr) {
                *reason = "upgrade image file is missing";
            }
            return false;
        }
        if (!infra::Path::MakeDirs(infra::Path::DirName(output_path)) ||
            !WriteZipEntryData(zip_package, *entry, output_path)) {
            if (reason != nullptr) {
                *reason = "failed to extract upgrade image";
            }
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
                ((i + 1) * 100ULL) / verified_manifest.commands.size());
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
