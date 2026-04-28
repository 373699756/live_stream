#include "auth_service.h"
#include "config_service.h"
#include "event_service.h"
#include "infra/status.h"
#include "infra/log.h"
#include "infra/service.h"
#include "logger_service.h"
#include "time_service.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kBusinessConfigPath = "configs/business_config.json";
constexpr const char* kAuthUsersPath = "configs/auth_users.json";
constexpr const char* kOperationLogPath = "build/runtime/operation.log";

struct ManagedService {
    std::string name;
    std::unique_ptr<infra::IService> service;
    bool initialized = false;
    bool started = false;
};

const char* PendingServices[] = {
    "system_service",
    "network_service",
    "osd_service",
    "media_service",
    "rtsp_service",
    "webrtc_service",
    "snapshot_service",
    "onvif_service",
    "alarm_service",
    "http_service",
    "upgrade_service",
};

live_stream::OperationAction MapAuthAction(
    live_stream::AuthAuditAction action) {
    switch (action) {
        case live_stream::AuthAuditAction::kLogin:
            return live_stream::OperationAction::kLogin;
        case live_stream::AuthAuditAction::kLogout:
            return live_stream::OperationAction::kLogout;
        case live_stream::AuthAuditAction::kAuthFailed:
            return live_stream::OperationAction::kAuthFailed;
        case live_stream::AuthAuditAction::kTokenExpired:
            return live_stream::OperationAction::kTokenExpired;
        case live_stream::AuthAuditAction::kPermissionDenied:
            return live_stream::OperationAction::kPermissionDenied;
    }
    return live_stream::OperationAction::kAuthFailed;
}

live_stream::OperationResult MapAuthResult(
    live_stream::AuthAuditResult result) {
    switch (result) {
        case live_stream::AuthAuditResult::kSuccess:
            return live_stream::OperationResult::kSuccess;
        case live_stream::AuthAuditResult::kFailed:
            return live_stream::OperationResult::kFailed;
        case live_stream::AuthAuditResult::kRejected:
            return live_stream::OperationResult::kRejected;
    }
    return live_stream::OperationResult::kFailed;
}

class AuthAuditToLoggerSink : public live_stream::IAuthAuditSink {
 public:
    explicit AuthAuditToLoggerSink(live_stream::ILoggerService* logger)
        : logger_(logger) {}

    infra::Status RecordAuthOperation(
        const live_stream::AuthAuditRecord& record) override {
        if (logger_ == nullptr) {
            return infra::Status::kInternalError;
        }

        live_stream::OperationRecord operation;
        operation.request_id = record.context.request_id;
        operation.user_name = record.user_name;
        operation.session_id = record.session_id;
        operation.client_ip = record.context.client_ip;
        operation.module = record.module;
        operation.action = MapAuthAction(record.action);
        operation.target = record.target;
        operation.result = MapAuthResult(record.result);
        operation.reason = record.reason;
        return logger_->RecordOperation(operation);
    }

 private:
    live_stream::ILoggerService* logger_;
};

std::unique_ptr<live_stream::ILoggerService> CreateLogger() {
    live_stream::LoggerServiceConfig config;
    config.operation_log_path = kOperationLogPath;
    return live_stream::CreateLoggerService(config);
}

std::unique_ptr<live_stream::IConfigService> CreateConfig() {
    live_stream::ConfigServiceOptions options;
    options.config_path = kBusinessConfigPath;
    options.default_config_path = "";
    options.create_storage_if_missing = true;
    return live_stream::CreateConfigService(options);
}

std::unique_ptr<live_stream::IAuthService> CreateAuth() {
    live_stream::AuthServiceOptions options;
    options.token_ttl_seconds = 30 * 60;
    options.max_sessions = 16;
    return live_stream::CreateAuthService(
        options,
        live_stream::CreateJsonAuthUserStore(kAuthUsersPath),
        live_stream::CreateSha256PasswordVerifier());
}

std::unique_ptr<live_stream::ITimeService> CreateTime() {
    live_stream::TimeServiceOptions options;
    options.default_ntp_config.enabled = false;
    return live_stream::CreateTimeService(options);
}

void StopStartedServices(std::vector<ManagedService>* services) {
    for (auto it = services->rbegin(); it != services->rend(); ++it) {
        if (!it->started) {
            continue;
        }
        INFRA_LOG_INFO("app", "Stop %s", it->name.c_str());
        it->service->Stop();
        it->started = false;
    }
}

void DeinitInitializedServices(std::vector<ManagedService>* services) {
    for (auto it = services->rbegin(); it != services->rend(); ++it) {
        if (!it->initialized) {
            continue;
        }
        INFRA_LOG_INFO("app", "Deinit %s", it->name.c_str());
        it->service->Deinit();
        it->initialized = false;
    }
}

infra::Status InitServices(std::vector<ManagedService>* services) {
    for (ManagedService& entry : *services) {
        INFRA_LOG_INFO("app", "Init %s", entry.name.c_str());
        const infra::Status error = entry.service->Init();
        if (error != infra::Status::kOk) {
            INFRA_LOG_ERROR("app", "Init %s failed: %s", entry.name.c_str(),
                            infra::StatusToString(error));
            return error;
        }
        entry.initialized = true;
    }
    return infra::Status::kOk;
}

infra::Status StartServices(std::vector<ManagedService>* services) {
    for (ManagedService& entry : *services) {
        INFRA_LOG_INFO("app", "Start %s", entry.name.c_str());
        const infra::Status error = entry.service->Start();
        if (error != infra::Status::kOk) {
            INFRA_LOG_ERROR("app", "Start %s failed: %s", entry.name.c_str(),
                            infra::StatusToString(error));
            return error;
        }
        entry.started = true;
    }
    return infra::Status::kOk;
}

void LogPendingServices() {
    for (const char* name : PendingServices) {
        INFRA_LOG_WARN("app", "%s pending: not wired into app registry",
                       name);
    }
}

}  // namespace

int main() {
    infra::LogConfig log_config;
    log_config.min_level = infra::LogLevel::kInfo;
    log_config.console_output = true;
    log_config.async_write = false;

    if (infra::Log::Init(log_config) != infra::Status::kOk) {
        return 1;
    }

    INFRA_LOG_INFO("app", "live_stream starting");

    std::vector<ManagedService> services;
    std::unique_ptr<live_stream::ILoggerService> logger = CreateLogger();
    live_stream::ILoggerService* logger_ptr = logger.get();
    std::unique_ptr<live_stream::IAuthService> auth = CreateAuth();
    live_stream::IAuthService* auth_ptr = auth.get();

    services.push_back({"logger_service", std::move(logger)});
    services.push_back({"config_service", CreateConfig()});
    services.push_back({"event_service", live_stream::CreateEventService()});
    services.push_back({"auth_service", std::move(auth)});
    services.push_back({"time_service", CreateTime()});

    AuthAuditToLoggerSink auth_audit_sink(logger_ptr);
    if (auth_ptr != nullptr) {
        (void)auth_ptr->SetAuditSink(&auth_audit_sink);
    }

    infra::Status error = InitServices(&services);
    if (error == infra::Status::kOk) {
        error = StartServices(&services);
    }

    LogPendingServices();
    StopStartedServices(&services);
    DeinitInitializedServices(&services);

    INFRA_LOG_INFO("app", "live_stream stopped");
    infra::Log::Shutdown();
    return error == infra::Status::kOk ? 0 : 1;
}
