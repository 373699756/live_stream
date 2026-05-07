#ifndef LIVE_STREAM_APP_CORE_SERVICES_H_
#define LIVE_STREAM_APP_CORE_SERVICES_H_

#include <memory>

#include "auth_service.h"
#include "config_service.h"
#include "event_service.h"
#include "logger_service.h"

namespace live_stream {

struct RuntimePaths {
  const char* business_config_path = nullptr;
  const char* default_config_path = nullptr;
  const char* auth_users_path = nullptr;
  const char* operation_log_path = nullptr;
};

class CoreServices {
 public:
  static CoreServices& Get();

  bool Start(const RuntimePaths& paths);
  void Stop();

  ILoggerService* logger() const { return logger_.get(); }
  IConfigService* config() const { return config_.get(); }
  IEventService* event() const { return event_.get(); }
  IAuthService* auth() const { return auth_.get(); }

 private:
  CoreServices() = default;
  ~CoreServices() = default;

  CoreServices(const CoreServices&) = delete;
  CoreServices& operator=(const CoreServices&) = delete;

  std::unique_ptr<ILoggerService> logger_;
  std::unique_ptr<IConfigService> config_;
  std::unique_ptr<IEventService> event_;
  std::unique_ptr<IAuthAuditSink> auth_audit_sink_;
  std::unique_ptr<IAuthService> auth_;
  bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_CORE_SERVICES_H_
