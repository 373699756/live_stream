#include "infra/fs.h"

#include <cerrno>
#include <sys/stat.h>

namespace infra {

std::string Path::Join(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (left.back() == '/') {
        return left + right;
    }
    return left + "/" + right;
}

std::string Path::DirName(const std::string& path) {
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

std::string Path::BaseName(const std::string& path) {
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

bool Path::Exists(const std::string& path) {
    struct stat path_stat;
    return !path.empty() && stat(path.c_str(), &path_stat) == 0;
}

Status Path::MakeDirs(const std::string& path) {
    if (path.empty()) {
        return Status::kInvalidParam;
    }
    if (Path::Exists(path)) {
        return Status::kOk;
    }

    std::string current;
    if (path.front() == '/') {
        current = "/";
    }

    size_t start = path.front() == '/' ? 1 : 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string part = path.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) {
            current = current == "/" ? current + part : Path::Join(current, part);
            if (!Path::Exists(current) && mkdir(current.c_str(), 0755) != 0 &&
                errno != EEXIST) {
                return Status::kIoError;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }

    return Status::kOk;
}

}  // namespace infra
