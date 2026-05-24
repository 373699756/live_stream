#include "upgrade_flash.h"

#include "infra/fs.h"
#include "infra/hash.h"
#include "linux_platform_common.h"

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <mtd/mtd-abi.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <unistd.h>

namespace live_stream {
namespace upgrade_flash {
namespace {

using linux_platform::Trim;

struct ProcMtdEntry {
    std::string device_name;
    std::string name;
    uint32_t size_bytes = 0;
    uint32_t erase_size = 0;
};

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

}  // namespace

bool IsPathOnTmpfs(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::istringstream stream(infra::File::ReadAll("/proc/mounts"));
    std::string dev;
    std::string dir;
    std::string type;
    std::string rest;
    std::string best_type;
    std::size_t best_len = 0;
    while (stream >> dev >> dir >> type) {
        std::getline(stream, rest);
        const bool exact_match = path == dir;
        const bool child_match =
            dir != "/" && path.size() > dir.size() &&
            path.compare(0, dir.size(), dir) == 0 &&
            path[dir.size()] == '/';
        const bool root_match = dir == "/";
        if ((exact_match || child_match || root_match) && dir.size() >= best_len) {
            best_type = type;
            best_len = dir.size();
        }
    }
    return best_type == "tmpfs" || best_type == "ramfs";
}

bool ValidateMtdLayoutForManifest(const UpgradeManifest& manifest,
                                  std::string* reason) {
    for (const UpgradeCommand& command : manifest.commands) {
        const int fd = open(command.partition_info.mtd_path.c_str(), O_RDONLY);
        if (fd < 0) {
            if (reason != nullptr) {
                *reason = "open MTD failed";
            }
            return false;
        }
        mtd_info_user info {};
        const bool ok =
            ValidateMtdDevice(command.partition_info, fd, &info, reason);
        close(fd);
        if (!ok) {
            return false;
        }
    }
    if (reason != nullptr) {
        reason->clear();
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

}  // namespace upgrade_flash
}  // namespace live_stream
