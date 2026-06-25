#include "application/startup_paths.h"

#include <cstdlib>
#include <cstring>

namespace live_stream {
namespace {

constexpr const char *kDefaultBusinessConfigPath =
    "configs/business_config.json";
constexpr const char *kDefaultConfigPath = "configs/default_config.json";
constexpr const char *kDefaultAuthUsersPath = "configs/auth_users.json";
constexpr const char *kDefaultOperationLogPath = "log/operation.log";
constexpr const char *kProductionConfigDir = "/config";
constexpr const char *kProductionOperationLogPath =
    "/data/log/operation_audit.log";

const char *ParseValueArg(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return nullptr;
}

ResolvedStartupPaths BuildStartupPaths(const char *config_dir) {
    ResolvedStartupPaths paths;
    if (config_dir != nullptr && config_dir[0] != '\0') {
        std::string base(config_dir);
        if (!base.empty() && base.back() != '/') {
            base += '/';
        }
        paths.business_config = base + "business_config.json";
        paths.default_config = base + "default_config.json";
        paths.auth_users = base + "auth_users.json";
    } else {
        paths.business_config = kDefaultBusinessConfigPath;
        paths.default_config = kDefaultConfigPath;
        paths.auth_users = kDefaultAuthUsersPath;
    }

    if (config_dir != nullptr &&
        std::strcmp(config_dir, kProductionConfigDir) == 0) {
        paths.operation_log = kProductionOperationLogPath;
    } else {
        paths.operation_log = kDefaultOperationLogPath;
    }
    return paths;
}

}  // namespace

StartupPaths ResolvedStartupPaths::ToStartupPaths() const {
    StartupPaths paths;
    paths.business_config_path = business_config.c_str();
    paths.default_config_path = default_config.c_str();
    paths.auth_users_path = auth_users.c_str();
    paths.operation_log_path = operation_log.c_str();
    return paths;
}

StartupOptions ResolveStartupOptions(int argc, char **argv) {
    StartupOptions options;
    const char *config_dir = ParseValueArg(argc, argv, "--config-dir");
    if (config_dir == nullptr) {
        config_dir = std::getenv("LIVE_STREAM_CONFIG_DIR");
    }
    options.paths = BuildStartupPaths(config_dir);

    const char *static_root = ParseValueArg(argc, argv, "--static-root");
    if (static_root == nullptr) {
        static_root = std::getenv("LIVE_STREAM_STATIC_ROOT");
    }
    if (static_root != nullptr) {
        options.static_root_override = static_root;
    }
    return options;
}

}  // namespace live_stream
