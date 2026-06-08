#include "runtime/app_runtime.h"

#include "infra/log.h"
#include "runtime/runtime_paths.h"

namespace {

bool InitProcess() {
    infra::LogConfig log_config;
    log_config.min_level = infra::LogLevel::kInfo;
    log_config.console_output = true;
    log_config.async_write = false;
    return infra::Log::Init(log_config);
}

}  // namespace

int main(int argc, char **argv) {
    if (!InitProcess()) {
        return 1;
    }

    const live_stream::RuntimeStartupOptions startup_options =
        live_stream::ResolveRuntimeStartupOptions(argc, argv);
    const live_stream::RuntimePaths paths =
        startup_options.paths.ToRuntimePaths();
    const char *static_root_override =
        startup_options.static_root_override.empty()
            ? nullptr
            : startup_options.static_root_override.c_str();

    Info("app", "live_stream starting");
    live_stream::InstallAppSignalHandlers();

    live_stream::AppRuntime &app = live_stream::AppRuntime::Get();
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
