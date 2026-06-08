#ifndef LIVE_STREAM_APP_RUNTIME_RUNTIME_PATHS_H_
#define LIVE_STREAM_APP_RUNTIME_RUNTIME_PATHS_H_

#include <string>

namespace live_stream {

struct RuntimePaths {
    const char *business_config_path = nullptr;
    const char *default_config_path = nullptr;
    const char *auth_users_path = nullptr;
    const char *operation_log_path = nullptr;
};

struct ResolvedRuntimePaths {
    std::string business_config;
    std::string default_config;
    std::string auth_users;
    std::string operation_log;

    RuntimePaths ToRuntimePaths() const;
};

struct RuntimeStartupOptions {
    ResolvedRuntimePaths paths;
    std::string static_root_override;
};

RuntimeStartupOptions ResolveRuntimeStartupOptions(int argc, char **argv);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_RUNTIME_RUNTIME_PATHS_H_
