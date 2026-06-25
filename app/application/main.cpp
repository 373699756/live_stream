#include "application/application.h"

#include "application/startup_paths.h"
#include "infra/log.h"

#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint32_t kProductionLogMaxSizeKb = 128;
constexpr uint32_t kProductionLogRotateFiles = 1;

bool InitProcess() {
    infra::LogConfig log_config;
    log_config.min_level = infra::LogLevel::kInfo;
    log_config.console_output = true;
    log_config.async_write = false;
    const char *log_path = std::getenv("LIVE_STREAM_LOG_PATH");
    if (log_path != nullptr && log_path[0] != '\0') {
        log_config.console_output = false;
        log_config.file_path = log_path;
        log_config.max_file_size_kb = kProductionLogMaxSizeKb;
        log_config.max_files = kProductionLogRotateFiles;
    }
    return infra::Log::Init(log_config);
}

}  // namespace

int main(int argc, char **argv) {
    if (!InitProcess()) {
        return 1;
    }

    const live_stream::StartupOptions startup_options =
        live_stream::ResolveStartupOptions(argc, argv);
    const live_stream::StartupPaths paths =
        startup_options.paths.ToStartupPaths();
    const char *static_root_override =
        startup_options.static_root_override.empty()
            ? nullptr
            : startup_options.static_root_override.c_str();

    Info("app", "live_stream starting");
    live_stream::InstallAppSignalHandlers();

    live_stream::Application &app = live_stream::Application::Get();
    bool ok = app.Start(paths, static_root_override);
    if (ok) {
        Info("app", "live_stream running");
        app.RunUntilSignal();
    }

    app.Stop();

    Info("app", "live_stream stopped");
    infra::Log::Shutdown();
    return ok ? 0 : 1;
}
