#include "app_runtime.h"
#include "infra/log.h"

namespace {

constexpr const char* kBusinessConfigPath = "configs/business_config.json";
constexpr const char* kDefaultConfigPath = "configs/default_config.json";
constexpr const char* kAuthUsersPath = "configs/auth_users.json";
constexpr const char* kOperationLogPath = "build/runtime/operation.log";

bool InitProcess() {
  infra::LogConfig log_config;
  log_config.min_level = infra::LogLevel::kInfo;
  log_config.console_output = true;
  log_config.async_write = false;
  return infra::Log::Init(log_config);
}

live_stream::RuntimePaths RuntimePaths() {
  live_stream::RuntimePaths paths;
  paths.business_config_path = kBusinessConfigPath;
  paths.default_config_path = kDefaultConfigPath;
  paths.auth_users_path = kAuthUsersPath;
  paths.operation_log_path = kOperationLogPath;
  return paths;
}

}  // namespace

int main() {
  if (!InitProcess()) {
    return 1;
  }

  INFRA_LOG_INFO("app", "live_stream starting");
  live_stream::InstallAppSignalHandlers();

  live_stream::AppRuntime& app = live_stream::AppRuntime::Get();
  bool ok = app.Start(RuntimePaths());
  if (ok) {
    INFRA_LOG_INFO("app", "live_stream running");
    app.RunUntilSignal();
  }

  app.Stop();

  INFRA_LOG_INFO("app", "live_stream stopped");
  infra::Log::Shutdown();
  return ok ? 0 : 1;
}
