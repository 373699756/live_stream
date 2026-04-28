#include "infra/fs.h"

#include <cstdio>
#include <sys/stat.h>

namespace infra {

Result<std::string> File::ReadAll(const std::string& path) {
    if (path.empty()) {
        return Result<std::string>::Fail(Status::kInvalidParam);
    }

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return Result<std::string>::Fail(Status::kNotFound);
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return Result<std::string>::Fail(Status::kIoError);
    }
    const long size = std::ftell(file);
    if (size < 0) {
        std::fclose(file);
        return Result<std::string>::Fail(Status::kIoError);
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return Result<std::string>::Fail(Status::kIoError);
    }

    std::string data;
    data.resize(static_cast<size_t>(size));
    if (size > 0) {
        const size_t read_size = std::fread(&data[0], 1, data.size(), file);
        if (read_size != data.size()) {
            std::fclose(file);
            return Result<std::string>::Fail(Status::kIoError);
        }
    }

    std::fclose(file);
    return Result<std::string>::Ok(data);
}

Status File::WriteAll(const std::string& path, const std::string& data) {
    if (path.empty()) {
        return Status::kInvalidParam;
    }

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return Status::kIoError;
    }
    const size_t write_size = std::fwrite(data.data(), 1, data.size(), file);
    const int close_result = std::fclose(file);
    if (write_size != data.size() || close_result != 0) {
        return Status::kIoError;
    }
    return Status::kOk;
}

Status File::Append(const std::string& path, const std::string& data) {
    if (path.empty()) {
        return Status::kInvalidParam;
    }

    std::FILE* file = std::fopen(path.c_str(), "ab");
    if (file == nullptr) {
        return Status::kIoError;
    }
    const size_t write_size = std::fwrite(data.data(), 1, data.size(), file);
    const int close_result = std::fclose(file);
    if (write_size != data.size() || close_result != 0) {
        return Status::kIoError;
    }
    return Status::kOk;
}

bool File::Exists(const std::string& path) {
    struct stat file_stat;
    return !path.empty() && stat(path.c_str(), &file_stat) == 0 &&
           S_ISREG(file_stat.st_mode);
}

Result<uint64_t> File::Size(const std::string& path) {
    if (path.empty()) {
        return Result<uint64_t>::Fail(Status::kInvalidParam);
    }

    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) != 0) {
        return Result<uint64_t>::Fail(Status::kNotFound);
    }
    if (!S_ISREG(file_stat.st_mode)) {
        return Result<uint64_t>::Fail(Status::kInvalidParam);
    }
    return Result<uint64_t>::Ok(static_cast<uint64_t>(file_stat.st_size));
}

Status File::Remove(const std::string& path) {
    if (path.empty()) {
        return Status::kInvalidParam;
    }
    return std::remove(path.c_str()) == 0 ? Status::kOk : Status::kNotFound;
}

Status File::Rename(const std::string& old_path, const std::string& new_path) {
    if (old_path.empty() || new_path.empty()) {
        return Status::kInvalidParam;
    }
    return std::rename(old_path.c_str(), new_path.c_str()) == 0 ? Status::kOk
                                                               : Status::kIoError;
}

}  // namespace infra
