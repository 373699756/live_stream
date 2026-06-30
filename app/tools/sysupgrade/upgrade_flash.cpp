#include "tools/sysupgrade/upgrade_flash.h"

#include "infra/fs.h"
#include "infra/hash.h"
#include "platform/linux/linux_text.h"

#include <cstdint>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/magic.h>
#include <mtd/mtd-abi.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
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

bool ReadProcMtdByDeviceName(const std::string& device_name,
                             ProcMtdEntry* entry) {
    if (entry == nullptr) {
        return false;
    }
    std::istringstream stream(infra::File::ReadAll("/proc/mtd"));
    std::string line;
    while (std::getline(stream, line)) {
        ProcMtdEntry parsed;
        if (ParseProcMtdLine(Trim(line), &parsed) &&
            parsed.device_name == device_name) {
            *entry = parsed;
            return true;
        }
    }
    return false;
}

bool ReadProcMtdByPartitionName(const std::string& partition_name,
                                ProcMtdEntry* entry) {
    if (entry == nullptr) {
        return false;
    }
    std::istringstream stream(infra::File::ReadAll("/proc/mtd"));
    std::string line;
    while (std::getline(stream, line)) {
        ProcMtdEntry parsed;
        if (ParseProcMtdLine(Trim(line), &parsed) &&
            parsed.name == partition_name) {
            *entry = parsed;
            return true;
        }
    }
    return false;
}

std::string MtdDeviceNameFromPath(const std::string& mtd_path) {
    constexpr const char* kMtdPathPrefix = "/dev/";
    if (mtd_path.compare(0, std::string(kMtdPathPrefix).size(),
                         kMtdPathPrefix) != 0) {
        return std::string();
    }
    const std::string device_name =
        mtd_path.substr(std::string(kMtdPathPrefix).size());
    constexpr const char* kMtdDirPrefix = "mtd/";
    if (device_name.compare(0, std::string(kMtdDirPrefix).size(),
                            kMtdDirPrefix) == 0) {
        return "mtd" + device_name.substr(std::string(kMtdDirPrefix).size());
    }
    return device_name;
}

std::string MtdDeviceNumber(const std::string& device_name) {
    constexpr const char* kMtdPrefix = "mtd";
    if (device_name.compare(0, std::string(kMtdPrefix).size(),
                            kMtdPrefix) != 0) {
        return std::string();
    }
    return device_name.substr(std::string(kMtdPrefix).size());
}

std::string Hex32(uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << value;
    return stream.str();
}

std::string ErrnoText(int error_number) {
    return std::string(strerror(error_number)) + " errno=" +
           std::to_string(error_number);
}

std::string ExpectedMtdText(const UpgradePartition& partition) {
    return "partition=" + partition.name + " mtd=" + partition.mtd_path +
           " size=" + Hex32(partition.size_bytes) +
           " erase=" + Hex32(partition.erase_size_bytes);
}

std::string ActualMtdText(const ProcMtdEntry& proc_entry,
                          const mtd_info_user& info) {
    return "proc=" + proc_entry.device_name + " name=" + proc_entry.name +
           " proc_size=" + Hex32(proc_entry.size_bytes) +
           " proc_erase=" + Hex32(proc_entry.erase_size) +
           " ioctl_size=" + Hex32(info.size) +
           " ioctl_erase=" + Hex32(info.erasesize);
}

std::string ProcMtdText(const ProcMtdEntry& proc_entry) {
    return "proc=" + proc_entry.device_name + " name=" + proc_entry.name +
           " proc_size=" + Hex32(proc_entry.size_bytes) +
           " proc_erase=" + Hex32(proc_entry.erase_size);
}

std::string ActualMtdText(const mtd_info_user& info) {
    return "proc=unavailable ioctl_size=" + Hex32(info.size) +
           " ioctl_erase=" + Hex32(info.erasesize);
}

bool BuildMtdPathCandidates(const UpgradePartition& partition,
                            std::vector<std::string>* candidates,
                            std::string* msg) {
    if (candidates == nullptr) {
        return false;
    }
    candidates->clear();
    if (!partition.mtd_path.empty()) {
        candidates->push_back(partition.mtd_path);
    }

    ProcMtdEntry proc_entry;
    if (!ReadProcMtdByPartitionName(partition.name, &proc_entry)) {
        if (msg != nullptr) {
            *msg = "MTD partition not found in /proc/mtd: expected " +
                   ExpectedMtdText(partition);
        }
        return !candidates->empty();
    }

    const std::string proc_path = "/dev/" + proc_entry.device_name;
    bool has_proc_path = false;
    for (const std::string& item : *candidates) {
        if (item == proc_path) {
            has_proc_path = true;
            break;
        }
    }
    if (!has_proc_path) {
        candidates->push_back(proc_path);
    }

    const std::string device_number = MtdDeviceNumber(proc_entry.device_name);
    if (!device_number.empty()) {
        const std::string mtd_dir_path = "/dev/mtd/" + device_number;
        bool has_mtd_dir_path = false;
        for (const std::string& item : *candidates) {
            if (item == mtd_dir_path) {
                has_mtd_dir_path = true;
                break;
            }
        }
        if (!has_mtd_dir_path) {
            candidates->push_back(mtd_dir_path);
        }
    }
    if (msg != nullptr) {
        msg->clear();
    }
    return true;
}

int OpenMtdDevice(const UpgradePartition& partition,
                  int flags,
                  std::string* opened_path,
                  std::string* msg) {
    std::vector<std::string> candidates;
    std::string candidate_msg;
    if (!BuildMtdPathCandidates(partition, &candidates, &candidate_msg) ||
        candidates.empty()) {
        if (msg != nullptr) {
            *msg = candidate_msg.empty()
                       ? "MTD partition device is not configured"
                       : candidate_msg;
        }
        return -1;
    }

    std::string failures;
    for (const std::string& path : candidates) {
        const int fd = open(path.c_str(), flags);
        if (fd >= 0) {
            if (opened_path != nullptr) {
                *opened_path = path;
            }
            if (msg != nullptr) {
                msg->clear();
            }
            return fd;
        }
        if (!failures.empty()) {
            failures += "; ";
        }
        failures += path + " " + ErrnoText(errno);
    }

    if (msg != nullptr) {
        *msg = "open MTD failed: expected " + ExpectedMtdText(partition) +
               " attempts=[" + failures + "]";
    }
    return -1;
}

bool ValidateMtdDevice(const UpgradePartition& partition,
                       const std::string& opened_path,
                       int fd,
                       mtd_info_user* info,
                       std::string* msg) {
    if (info == nullptr || ioctl(fd, MEMGETINFO, info) != 0) {
        if (msg != nullptr) {
            *msg = "MEMGETINFO failed: path=" + opened_path + " " +
                   ErrnoText(errno);
        }
        return false;
    }
    if (info->type != MTD_NORFLASH || info->erasesize == 0) {
        if (msg != nullptr) {
            *msg = "MTD device is not writable NOR flash: path=" +
                   opened_path + " type=" + std::to_string(info->type) +
                   " erase=" + Hex32(info->erasesize);
        }
        return false;
    }
    ProcMtdEntry proc_entry;
    const std::string expected_device_name =
        MtdDeviceNameFromPath(opened_path);
    const bool proc_mtd_available =
        !expected_device_name.empty() &&
        ReadProcMtdByDeviceName(expected_device_name, &proc_entry);
    const bool ioctl_layout_ok =
        info->size == partition.size_bytes &&
        info->erasesize == partition.erase_size_bytes;
    const bool proc_layout_ok =
        !proc_mtd_available ||
        (proc_entry.name == partition.name &&
         proc_entry.size_bytes == partition.size_bytes &&
         proc_entry.erase_size == partition.erase_size_bytes);
    if (!ioctl_layout_ok || !proc_layout_ok) {
        if (msg != nullptr) {
            *msg = "MTD partition layout mismatch: expected " +
                   ExpectedMtdText(partition) + " opened=" + opened_path +
                   " actual " +
                   (proc_mtd_available ? ActualMtdText(proc_entry, *info)
                                       : ActualMtdText(*info));
        }
        return false;
    }
    ProcMtdEntry named_entry;
    if (proc_mtd_available &&
        ReadProcMtdByPartitionName(partition.name, &named_entry) &&
        named_entry.device_name != proc_entry.device_name) {
        if (msg != nullptr) {
            *msg = "MTD partition name points to different device: expected " +
                   ExpectedMtdText(partition) + " opened=" + opened_path +
                   " named " + ProcMtdText(named_entry);
        }
        return false;
    }
    return true;
}

bool ReadFully(int fd, uint8_t* data, std::size_t size) {
    if (data == nullptr && size > 0) {
        return false;
    }
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t read_size = read(fd, data + offset, size - offset);
        if (read_size <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(read_size);
    }
    return true;
}

bool IsUpgradeImageMagicValid(const UpgradeCommand& command,
                           int image_fd,
                           std::string* msg) {
    uint8_t magic[4] = {0};
    if (lseek(image_fd, 0, SEEK_SET) < 0 ||
        !ReadFully(image_fd, magic, sizeof(magic)) ||
        lseek(image_fd, 0, SEEK_SET) < 0) {
        if (msg != nullptr) {
            *msg = "read image magic failed";
        }
        return false;
    }
    bool ok = false;
    if (command.partition == "kernel") {
        ok = magic[0] == 0x27U && magic[1] == 0x05U && magic[2] == 0x19U &&
             magic[3] == 0x56U;
    } else if (command.partition == "bin" || command.partition == "web") {
        ok = magic[0] == 'h' && magic[1] == 's' && magic[2] == 'q' &&
             magic[3] == 's';
    } else if (command.partition == "config") {
        ok = magic[0] == 0x85U && magic[1] == 0x19U;
    } else if (command.partition == "rootfs") {
        ok = magic[0] == 0x85U && magic[1] == 0x19U;
    }
    if (!ok && msg != nullptr) {
        *msg = "upgrade image magic mismatch";
    }
    return ok;
}

std::string Sha256FdHex(int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return std::string();
    }
    infra::Sha256 sha;
    uint8_t buffer[64 * 1024];
    while (true) {
        const ssize_t read_size = read(fd, buffer, sizeof(buffer));
        if (read_size < 0) {
            return std::string();
        }
        if (read_size == 0) {
            break;
        }
        sha.Update(buffer, static_cast<std::size_t>(read_size));
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return std::string();
    }
    return sha.FinishHex();
}

bool OpenAndValidateImage(const UpgradeCommand& command,
                          const std::string& image_path,
                          int* image_fd,
                          std::string* msg) {
    if (image_fd == nullptr) {
        return false;
    }
    *image_fd = -1;
    const int fd = open(image_path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        if (msg != nullptr) {
            *msg = "open upgrade image failed";
        }
        return false;
    }
    struct stat file_stat;
    if (fstat(fd, &file_stat) != 0 ||
        !S_ISREG(file_stat.st_mode)) {
        close(fd);
        if (msg != nullptr) {
            *msg = "upgrade image is not a regular file";
        }
        return false;
    }
    if (file_stat.st_size <= 0 ||
        static_cast<uint64_t>(file_stat.st_size) != command.size_bytes ||
        static_cast<uint64_t>(file_stat.st_size) >
            command.partition_info.size_bytes) {
        close(fd);
        if (msg != nullptr) {
            *msg = "upgrade image size mismatch";
        }
        return false;
    }
    const std::string image_sha256 = Sha256FdHex(fd);
    if (image_sha256.empty() || image_sha256 != command.sha256) {
        close(fd);
        if (msg != nullptr) {
            *msg = "upgrade image sha256 mismatch";
        }
        return false;
    }
    if (!IsUpgradeImageMagicValid(command, fd, msg)) {
        close(fd);
        return false;
    }
    *image_fd = fd;
    return true;
}

void ReportMtdProgress(const MtdProgressCallback& progress_callback,
                       uint32_t progress_percent,
                       const std::string& stage) {
    if (progress_callback) {
        progress_callback(progress_percent > 100U ? 100U : progress_percent,
                          stage);
    }
}

bool WriteAllBytes(int fd,
                   int image_fd,
                   uint64_t total_size,
                   const MtdProgressCallback& progress_callback,
                   uint64_t* written) {
    if (written == nullptr) {
        return false;
    }
    if (lseek(image_fd, 0, SEEK_SET) < 0) {
        return false;
    }
    uint8_t buffer[64 * 1024];
    *written = 0;
    bool ok = true;
    uint32_t last_progress = 0;
    while (ok) {
        const ssize_t read_size = read(image_fd, buffer, sizeof(buffer));
        if (read_size < 0) {
            ok = false;
            break;
        }
        if (read_size == 0) {
            break;
        }
        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(read_size)) {
            const ssize_t write_size =
                write(fd, buffer + offset,
                      static_cast<std::size_t>(read_size) - offset);
            if (write_size <= 0) {
                ok = false;
                break;
            }
            offset += static_cast<std::size_t>(write_size);
            *written += static_cast<uint64_t>(write_size);
            if (total_size > 0) {
                const uint32_t progress = 25U + static_cast<uint32_t>(
                                                    (*written * 55ULL) /
                                                    total_size);
                if (progress != last_progress) {
                    last_progress = progress;
                    ReportMtdProgress(progress_callback, progress,
                                      "writing mtd image");
                }
            }
        }
    }
    return ok;
}

bool ReadBackSha256(int fd,
                    uint64_t size,
                    const MtdProgressCallback& progress_callback,
                    std::string* sha256) {
    if (sha256 == nullptr || lseek(fd, 0, SEEK_SET) < 0) {
        return false;
    }
    infra::Sha256 sha;
    uint8_t buffer[64 * 1024];
    uint64_t remaining = size;
    uint64_t verified = 0;
    uint32_t last_progress = 80U;
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
        verified += static_cast<uint64_t>(read_size);
        if (size > 0) {
            const uint32_t progress =
                80U + static_cast<uint32_t>((verified * 20ULL) / size);
            if (progress != last_progress) {
                last_progress = progress;
                ReportMtdProgress(progress_callback, progress,
                                  "verifying mtd readback");
            }
        }
    }
    *sha256 = sha.FinishHex();
    return true;
}

}  // namespace

bool IsPathOnTmpfs(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    struct statfs fs_info;
    if (statfs(path.c_str(), &fs_info) != 0) {
        return false;
    }
    const unsigned long fs_type = static_cast<unsigned long>(fs_info.f_type);
    return fs_type == static_cast<unsigned long>(TMPFS_MAGIC) ||
           fs_type == static_cast<unsigned long>(RAMFS_MAGIC);
}

bool ValidateMtdLayoutForManifest(const UpgradeManifest& manifest,
                                  std::string* msg) {
    for (const UpgradeCommand& command : manifest.commands) {
        std::string opened_path;
        const int fd = OpenMtdDevice(command.partition_info, O_RDONLY,
                                     &opened_path, msg);
        if (fd < 0) {
            return false;
        }
        mtd_info_user info{};
        const bool ok = ValidateMtdDevice(command.partition_info, opened_path,
                                          fd, &info, msg);
        close(fd);
        if (!ok) {
            return false;
        }
    }
    if (msg != nullptr) {
        msg->clear();
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

bool UnmountIfMounted(const std::string& mount_point, std::string* msg) {
    if (!IsMounted(mount_point)) {
        return true;
    }
    if (umount(mount_point.c_str()) != 0) {
        if (msg != nullptr) {
            *msg = "unmount failed";
        }
        return false;
    }
    return true;
}

bool Remount(const UpgradePartition& partition, std::string* msg) {
    if (partition.block_path.empty() || partition.mount_point.empty() ||
        partition.fs_type.empty()) {
        return true;
    }
    if (mount(partition.block_path.c_str(), partition.mount_point.c_str(),
              partition.fs_type.c_str(), MS_RDONLY,
              partition.mount_options.empty() ? nullptr
                                              : partition.mount_options.c_str()) !=
        0) {
        if (msg != nullptr) {
            *msg = "remount failed";
        }
        return false;
    }
    return true;
}

bool WriteMtdImage(const UpgradeCommand& command,
                   const std::string& image_path,
                   std::string* msg) {
    return WriteMtdImage(command, image_path, MtdProgressCallback(), msg);
}

bool WriteMtdImage(const UpgradeCommand& command,
                   const std::string& image_path,
                   MtdProgressCallback progress_callback,
                   std::string* msg) {
    const UpgradePartition& partition = command.partition_info;
    int image_fd = -1;
    if (!OpenAndValidateImage(command, image_path, &image_fd, msg)) {
        return false;
    }
    ReportMtdProgress(progress_callback, 0, "validating mtd image");
    std::string opened_path;
    const int fd = OpenMtdDevice(partition, O_RDWR, &opened_path, msg);
    if (fd < 0) {
        close(image_fd);
        return false;
    }

    mtd_info_user info{};
    bool ok = ValidateMtdDevice(partition, opened_path, fd, &info, msg);
    if (ok) {
        uint32_t erase_index = 0;
        const uint32_t erase_total =
            info.erasesize == 0 ? 0 : info.size / info.erasesize;
        for (uint32_t offset = 0; offset < info.size; offset += info.erasesize) {
            erase_info_user erase{};
            erase.start = offset;
            erase.length = info.erasesize;
            if (ioctl(fd, MEMERASE, &erase) != 0) {
                if (msg != nullptr) {
                    *msg = "MEMERASE failed: path=" + opened_path +
                           " offset=" + Hex32(offset) + " length=" +
                           Hex32(info.erasesize) + " " + ErrnoText(errno);
                }
                ok = false;
                break;
            }
            ++erase_index;
            if (erase_total > 0) {
                const uint32_t progress =
                    static_cast<uint32_t>((erase_index * 25ULL) / erase_total);
                ReportMtdProgress(progress_callback, progress,
                                  "erasing mtd partition");
            }
        }
    }
    uint64_t written = 0;
    if (ok && lseek(fd, 0, SEEK_SET) < 0) {
        if (msg != nullptr) {
            *msg = "seek MTD failed: path=" + opened_path + " " +
                   ErrnoText(errno);
        }
        ok = false;
    }
    if (ok && !WriteAllBytes(fd, image_fd, command.size_bytes,
                             progress_callback, &written)) {
        if (msg != nullptr) {
            *msg = "write MTD failed: path=" + opened_path + " " +
                   ErrnoText(errno);
        }
        ok = false;
    }
    if (ok && written != command.size_bytes) {
        if (msg != nullptr) {
            *msg = "written size mismatch";
        }
        ok = false;
    }
    std::string readback_sha256;
    if (ok && (!ReadBackSha256(fd, written, progress_callback,
                               &readback_sha256) ||
               readback_sha256 != command.sha256)) {
        if (msg != nullptr) {
            *msg = "MTD readback sha256 mismatch";
        }
        ok = false;
    }
    if (ok) {
        ReportMtdProgress(progress_callback, 100, "mtd image written");
    }
    close(fd);
    close(image_fd);
    return ok;
}

}  // namespace upgrade_flash
}  // namespace live_stream
