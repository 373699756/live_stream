#include "linux_platform_common.h"

#include "infra/fs.h"

#include <ctime>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace linux_platform {

std::string Trim(const std::string &value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' ||
          value[begin] == '\n')) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                         value[end - 1] == '\r' || value[end - 1] == '\n')) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string ReadFirstText(const std::vector<std::string> &paths) {
  for (const std::string &path : paths) {
    const std::string value = Trim(infra::File::ReadAll(path));
    if (!value.empty()) {
      return value;
    }
  }
  return std::string();
}

int64_t ReadSystemTimeMs() {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
    return 0;
  }
  return static_cast<int64_t>(now.tv_sec) * 1000LL +
         static_cast<int64_t>(now.tv_nsec / 1000000LL);
}

bool SetSystemTimeMsInternal(int64_t unix_time_ms) {
  if (unix_time_ms <= 0) {
    return false;
  }
  struct timespec value;
  value.tv_sec = static_cast<time_t>(unix_time_ms / 1000LL);
  value.tv_nsec = static_cast<long>((unix_time_ms % 1000LL) * 1000000LL);
  return clock_settime(CLOCK_REALTIME, &value) == 0;
}

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
