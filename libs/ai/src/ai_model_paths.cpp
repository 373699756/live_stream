#include "ai_model_paths.h"

#include "infra/fs.h"

#include <limits.h>
#include <unistd.h>

namespace live_stream {
namespace ai_internal {
namespace {

bool IsAbsolutePath(const std::string &path) {
    return !path.empty() && path[0] == '/';
}

std::string JoinPath(const std::string &base, const std::string &path) {
    if (base.empty()) {
        return path;
    }
    if (base.back() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

std::string ParentDir(const std::string &path) {
    const std::string::size_type slash = path.rfind('/');
    if (slash == std::string::npos || slash == 0) {
        return slash == 0 ? "/" : "";
    }
    return path.substr(0, slash);
}

std::string ExecutableDir() {
    char buffer[PATH_MAX];
    const ssize_t size = readlink("/proc/self/exe", buffer,
                                 sizeof(buffer) - 1);
    if (size <= 0) {
        return "";
    }
    buffer[size] = '\0';
    return ParentDir(buffer);
}

bool FileExists(const std::string &path) {
    return !path.empty() && infra::File::Size(path) > 0;
}

}  // namespace

std::string ResolveAiModelPath(const std::string &path) {
    if (path.empty() || IsAbsolutePath(path) || FileExists(path)) {
        return path;
    }

    const std::string exe_dir = ExecutableDir();
    if (exe_dir.empty()) {
        return path;
    }

    const std::string beside_exe = JoinPath(exe_dir, path);
    if (FileExists(beside_exe)) {
        return beside_exe;
    }

    const std::string beside_package = JoinPath(ParentDir(exe_dir), path);
    if (FileExists(beside_package)) {
        return beside_package;
    }

    return path;
}

bool AiModelFileExists(const std::string &path) {
    return FileExists(ResolveAiModelPath(path));
}

}  // namespace ai_internal
}  // namespace live_stream
