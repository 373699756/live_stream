#include "platform/linux/linux_process.h"

#include <sys/wait.h>
#include <unistd.h>

namespace live_stream {
namespace linux_platform {

int RunCommand(const std::vector<std::string> &argv) {
    if (argv.empty() || argv.front().empty()) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        std::vector<char *> args;
        args.reserve(argv.size() + 1);
        for (const std::string &item : argv) {
            args.push_back(const_cast<char *>(item.c_str()));
        }
        args.push_back(nullptr);
        execvp(args.front(), args.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

bool RunAny(const std::vector<std::vector<std::string>> &commands) {
    for (const std::vector<std::string> &command : commands) {
        if (RunCommand(command) == 0) {
            return true;
        }
    }
    return false;
}

bool IsExecutable(const std::string &path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

}  // namespace linux_platform
}  // namespace live_stream
