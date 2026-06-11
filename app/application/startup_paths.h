#ifndef LIVE_STREAM_APP_APPLICATION_STARTUP_PATHS_H_
#define LIVE_STREAM_APP_APPLICATION_STARTUP_PATHS_H_

#include <string>

namespace live_stream {

struct StartupPaths {
    const char *business_config_path = nullptr;
    const char *default_config_path = nullptr;
    const char *auth_users_path = nullptr;
    const char *operation_log_path = nullptr;
};

struct ResolvedStartupPaths {
    std::string business_config;
    std::string default_config;
    std::string auth_users;
    std::string operation_log;

    StartupPaths ToStartupPaths() const;
};

struct StartupOptions {
    ResolvedStartupPaths paths;
    std::string static_root_override;
};

StartupOptions ResolveStartupOptions(int argc, char **argv);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_APPLICATION_STARTUP_PATHS_H_
