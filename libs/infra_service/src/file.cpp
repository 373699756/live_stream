#include "infra/fs.h"

#include <cstdio>
#include <sys/stat.h>

namespace infra {

std::string File::ReadAll(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::string();
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return std::string();
    }
    const long size = std::ftell(file);
    if (size < 0) {
        std::fclose(file);
        return std::string();
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return std::string();
    }

    std::string data;
    data.resize(static_cast<size_t>(size));
    if (size > 0) {
        const size_t read_size = std::fread(&data[0], 1, data.size(), file);
        if (read_size != data.size()) {
            std::fclose(file);
            return std::string();
        }
    }

    std::fclose(file);
    return data;
}

bool File::WriteAll(const std::string& path, const std::string& data) {
    if (path.empty()) {
        return false;
    }

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const size_t write_size = std::fwrite(data.data(), 1, data.size(), file);
    const int close_result = std::fclose(file);
    return write_size == data.size() && close_result == 0;
}

bool File::Append(const std::string& path, const std::string& data) {
    if (path.empty()) {
        return false;
    }

    std::FILE* file = std::fopen(path.c_str(), "ab");
    if (file == nullptr) {
        return false;
    }
    const size_t write_size = std::fwrite(data.data(), 1, data.size(), file);
    const int close_result = std::fclose(file);
    return write_size == data.size() && close_result == 0;
}

bool File::Exists(const std::string& path) {
    struct stat file_stat;
    return !path.empty() && stat(path.c_str(), &file_stat) == 0 &&
           S_ISREG(file_stat.st_mode);
}

uint64_t File::Size(const std::string& path) {
    if (path.empty()) {
        return 0;
    }

    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(file_stat.st_size);
}

bool File::Remove(const std::string& path) {
    return !path.empty() && std::remove(path.c_str()) == 0;
}

bool File::Rename(const std::string& old_path, const std::string& new_path) {
    return !old_path.empty() && !new_path.empty() &&
           std::rename(old_path.c_str(), new_path.c_str()) == 0;
}

}  // namespace infra
