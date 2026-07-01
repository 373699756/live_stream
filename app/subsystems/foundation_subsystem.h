#ifndef LIVE_STREAM_APP_SUBSYSTEMS_FOUNDATION_SUBSYSTEM_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_FOUNDATION_SUBSYSTEM_H_

#include <memory>

#include "auth.h"
#include "config.h"
#include "event.h"
#include "logger.h"
#include "application/startup_paths.h"

namespace live_stream {

class FoundationSubsystem {
public:
    static FoundationSubsystem& Get();

    bool Start(const StartupPaths& paths);
    void Stop();

    ILogger* logger() const { return logger_.get(); }
    IConfig* config() const { return config_.get(); }
    event::EventCenter* event() const {
        return event_ != nullptr ? event_->center() : nullptr;
    }
    IAuth* auth() const { return auth_.get(); }

private:
    FoundationSubsystem() = default;
    ~FoundationSubsystem() = default;

    FoundationSubsystem(const FoundationSubsystem&) = delete;
    FoundationSubsystem& operator=(const FoundationSubsystem&) = delete;

    std::unique_ptr<ILogger> logger_;
    std::unique_ptr<IConfig> config_;
    std::unique_ptr<event::EventBus> event_;
    std::unique_ptr<IAuthAuditSink> auth_audit_sink_;
    std::unique_ptr<IAuth> auth_;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_FOUNDATION_SUBSYSTEM_H_
