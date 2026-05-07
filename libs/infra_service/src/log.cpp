#include "infra/log.h"

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>

namespace infra {
namespace {

constexpr size_t kMaxFormattedLogSize = 2048;
constexpr size_t kMaxAsyncQueueSize = 1024;

std::mutex g_log_mutex;
std::condition_variable g_log_condition;
LogConfig g_config;
bool g_initialized = false;
bool g_stopping = false;
std::deque<std::string> g_async_lines;
std::thread g_worker;
std::FILE* g_file = nullptr;

const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kFatal:
            return "FATAL";
    }

    return "UNKNOWN";
}

bool ShouldWrite(LogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(g_config.min_level);
}

int64_t NowMillis() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string BuildLine(LogLevel level,
                      const char* module,
                      const char* file,
                      int line,
                      const char* fmt,
                      va_list args) {
    char message[kMaxFormattedLogSize];
    std::vsnprintf(message, sizeof(message), fmt == nullptr ? "" : fmt, args);

    char prefix[512];
    std::snprintf(prefix, sizeof(prefix), "[%lld][%s][%s][%s:%d] ",
                  static_cast<long long>(NowMillis()),
                  LevelToString(level),
                  module == nullptr ? "unknown" : module,
                  file == nullptr ? "unknown" : file,
                  line);

    std::string result(prefix);
    result += message;
    result += "\n";
    return result;
}

uint64_t CurrentLogFileSize() {
    if (g_config.file_path.empty()) {
        return 0;
    }

    struct stat file_stat;
    if (stat(g_config.file_path.c_str(), &file_stat) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(file_stat.st_size);
}

void OpenFileLocked() {
    if (g_file != nullptr || g_config.file_path.empty()) {
        return;
    }
    g_file = std::fopen(g_config.file_path.c_str(), "ab");
}

void CloseFileLocked() {
    if (g_file != nullptr) {
        std::fflush(g_file);
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void RotateFileIfNeededLocked() {
    if (g_config.file_path.empty() || g_config.max_file_size_kb == 0) {
        return;
    }

    const uint64_t max_bytes =
        static_cast<uint64_t>(g_config.max_file_size_kb) * 1024U;
    if (CurrentLogFileSize() < max_bytes) {
        return;
    }

    CloseFileLocked();
    if (g_config.max_file_count == 0) {
        std::remove(g_config.file_path.c_str());
        OpenFileLocked();
        return;
    }

    for (uint32_t i = g_config.max_file_count; i > 1; --i) {
        const std::string from = g_config.file_path + "." + std::to_string(i - 1);
        const std::string to = g_config.file_path + "." + std::to_string(i);
        std::rename(from.c_str(), to.c_str());
    }
    const std::string first = g_config.file_path + ".1";
    std::rename(g_config.file_path.c_str(), first.c_str());
    OpenFileLocked();
}

void WriteLineLocked(const std::string& line) {
    if (g_config.console_output) {
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fflush(stderr);
    }

    if (!g_config.file_path.empty()) {
        OpenFileLocked();
        if (g_file != nullptr) {
            RotateFileIfNeededLocked();
            std::fwrite(line.data(), 1, line.size(), g_file);
            std::fflush(g_file);
        }
    }
}

void WorkerMain() {
    while (true) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(g_log_mutex);
            g_log_condition.wait(lock, []() {
                return g_stopping || !g_async_lines.empty();
            });
            if (g_stopping && g_async_lines.empty()) {
                break;
            }
            line = std::move(g_async_lines.front());
            g_async_lines.pop_front();
            WriteLineLocked(line);
        }
    }
}

void StopWorker() {
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_stopping = true;
    }
    g_log_condition.notify_all();
    if (g_worker.joinable()) {
        g_worker.join();
    }
}

}  // namespace

bool Log::Init(const LogConfig& config) {
    Shutdown();

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_config = config;
        g_stopping = false;
        g_initialized = true;
        g_async_lines.clear();
        if (!g_config.file_path.empty()) {
            OpenFileLocked();
            if (g_file == nullptr) {
                g_initialized = false;
                return false;
            }
        }
    }

    if (config.async_write) {
        g_worker = std::thread(WorkerMain);
    }

    return true;
}

void Log::Shutdown() {
    StopWorker();

    std::lock_guard<std::mutex> lock(g_log_mutex);
    while (!g_async_lines.empty()) {
        const std::string line = std::move(g_async_lines.front());
        g_async_lines.pop_front();
        WriteLineLocked(line);
    }
    CloseFileLocked();
    g_initialized = false;
    g_stopping = false;
}

void Log::Write(LogLevel level,
                const char* module,
                const char* file,
                int line,
                const char* fmt,
                ...) {
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if (!g_initialized) {
            g_config = LogConfig{};
            g_initialized = true;
            g_stopping = false;
        }
        if (!ShouldWrite(level)) {
            return;
        }
    }

    va_list args;
    va_start(args, fmt);
    const std::string log_line = BuildLine(level, module, file, line, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_config.async_write && g_worker.joinable()) {
        if (g_async_lines.size() >= kMaxAsyncQueueSize) {
            if (level == LogLevel::kTrace || level == LogLevel::kDebug) {
                return;
            }
            g_async_lines.pop_front();
        }
        g_async_lines.push_back(log_line);
        g_log_condition.notify_one();
        return;
    }

    WriteLineLocked(log_line);
}

}  // namespace infra
