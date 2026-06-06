/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: fs.h
 * Brief: Defines infra filesystem path and file helpers.
 */

#ifndef LIVE_STREAM_INFRA_FS_H_
#define LIVE_STREAM_INFRA_FS_H_

#include <cstdint>
#include <string>

namespace infra {

class File {
public:
    static std::string ReadAll(const std::string& path);
    static bool WriteAll(const std::string& path, const std::string& data);
    static bool Append(const std::string& path, const std::string& data);
    static bool Exists(const std::string& path);
    static uint64_t Size(const std::string& path);
    static bool Remove(const std::string& path);
    static bool Rename(const std::string& old_path,
                       const std::string& new_path);
};

class Path {
public:
    static std::string Join(const std::string& left, const std::string& right);
    static std::string DirName(const std::string& path);
    static std::string BaseName(const std::string& path);
    static bool Exists(const std::string& path);
    static bool MakeDirs(const std::string& path);
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_FS_H_
