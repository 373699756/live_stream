#include "infra/log.h"

#include "infra/fs.h"

int main() {
    const std::string log_path = "/tmp/live_stream_infra_log_test.log";
    infra::File::Remove(log_path);

    infra::LogConfig config;
    config.min_level = infra::LogLevel::kInfo;
    config.file_path = log_path;
    config.console_output = false;
    config.async_write = true;

    if (!infra::Log::Init(config)) {
        return 1;
    }

    Debug("test", "filtered log");
    Info("test", "info log %d", 1);
    Warn("test", "warn log");
    Error("test", "error log");
    infra::Log::Shutdown();

    const std::string content = infra::File::ReadAll(log_path);
    if (content.empty()) {
        return 2;
    }
    if (content.find("info log 1") == std::string::npos ||
        content.find("filtered log") != std::string::npos) {
        return 3;
    }
    infra::File::Remove(log_path);
    return 0;
}
