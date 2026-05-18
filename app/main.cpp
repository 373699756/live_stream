#include "app_runtime.h"
#include "infra/log.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Default relative paths (resolved from the process working directory).
constexpr const char *kDefaultBusinessConfigPath =
    "configs/business_config.json";
constexpr const char *kDefaultConfigPath = "configs/default_config.json";
constexpr const char *kDefaultAuthUsersPath = "configs/auth_users.json";
constexpr const char *kDefaultOperationLogPath =
    "log/operation.log";

// ResolvedPaths owns the string storage that RuntimePaths points into.
// Paths may be overridden by:
//   1. CLI argument:      --config-dir <dir>  (overrides configs/* paths)
//   2. Environment var:   LIVE_STREAM_CONFIG_DIR=<dir>
// If neither is set the default relative paths above are used as-is.
struct ResolvedPaths {
    std::string business_config;
    std::string default_config;
    std::string auth_users;
    std::string operation_log;

    live_stream::RuntimePaths ToRuntimePaths() const {
        live_stream::RuntimePaths p;
        p.business_config_path = business_config.c_str();
        p.default_config_path = default_config.c_str();
        p.auth_users_path = auth_users.c_str();
        p.operation_log_path = operation_log.c_str();
        return p;
    }
};

// If a config_dir is provided the configs/* paths are relocated under it.
// The operation log is always placed at ../log/operation.log relative
// to the working directory (or unchanged when no override is given).
ResolvedPaths BuildPaths(const char *config_dir) {
    ResolvedPaths r;
    if (config_dir != nullptr && config_dir[0] != '\0') {
        std::string base(config_dir);
        if (!base.empty() && base.back() != '/') {
            base += '/';
        }
        r.business_config = base + "business_config.json";
        r.default_config = base + "default_config.json";
        r.auth_users = base + "auth_users.json";
    } else {
        r.business_config = kDefaultBusinessConfigPath;
        r.default_config = kDefaultConfigPath;
        r.auth_users = kDefaultAuthUsersPath;
    }
    r.operation_log = kDefaultOperationLogPath;
    return r;
}

// Parse --config-dir <path> from argv; returns nullptr if not present.
const char *ParseConfigDirArg(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config-dir") == 0) {
            return argv[i + 1];
        }
    }
    return nullptr;
}

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

    // Resolve config dir: CLI arg wins over env var; env var wins over default.
    const char *config_dir = ParseConfigDirArg(argc, argv);
    if (config_dir == nullptr) {
        config_dir = std::getenv("LIVE_STREAM_CONFIG_DIR");
    }

    const ResolvedPaths resolved = BuildPaths(config_dir);
    const live_stream::RuntimePaths paths = resolved.ToRuntimePaths();

    INFRA_LOG_INFO("app", "live_stream starting");
    live_stream::InstallAppSignalHandlers();

    live_stream::AppRuntime &app = live_stream::AppRuntime::Get();
    bool ok = app.Start(paths);
    if (ok) {
        INFRA_LOG_INFO("app", "live_stream running");
        app.RunUntilSignal();
    }

    app.Stop();

    INFRA_LOG_INFO("app", "live_stream stopped");
    infra::Log::Shutdown();
    return ok ? 0 : 1;
}
