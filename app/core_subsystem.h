#ifndef LIVE_STREAM_APP_CORE_SUBSYSTEM_H_
#define LIVE_STREAM_APP_CORE_SUBSYSTEM_H_

#include <memory>

#include "auth.h"
#include "config.h"
#include "event.h"
#include "logger.h"

namespace live_stream {

struct RuntimePaths {
    const char* business_config_path = nullptr;
    const char* default_config_path = nullptr;
    const char* auth_users_path = nullptr;
    const char* operation_log_path = nullptr;
};

class CoreSubsystem {
public:
    static CoreSubsystem& Get();

    bool Start(const RuntimePaths& paths);
    void Stop();

    ILogger* logger() const { return logger_.get(); }
    IConfig* config() const { return config_.get(); }
    IEvent* event() const { return event_.get(); }
    IAuth* auth() const { return auth_.get(); }

private:
    CoreSubsystem() = default;
    ~CoreSubsystem() = default;

    CoreSubsystem(const CoreSubsystem&) = delete;
    CoreSubsystem& operator=(const CoreSubsystem&) = delete;

    std::unique_ptr<ILogger> logger_;
    std::unique_ptr<IConfig> config_;
    std::unique_ptr<IEvent> event_;
    std::unique_ptr<IAuthAuditSink> auth_audit_sink_;
    std::unique_ptr<IAuth> auth_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_CORE_SUBSYSTEM_H_
