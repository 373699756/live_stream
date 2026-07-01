#include "subsystems/foundation_subsystem.h"

#include "infra/log.h"
#include "runtime.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

constexpr uint64_t kProductionOperationLogMaxBytes = 128U * 1024U;
constexpr uint32_t kProductionOperationLogRotateFiles = 2;

bool IsProductionDataPath(const char* path) {
    return path != nullptr && std::string(path).find("/data/") == 0;
}

OperationAction MapAuthAction(AuthAuditAction action) {
    switch (action) {
        case AuthAuditAction::kLogin:
            return OperationAction::kLogin;
        case AuthAuditAction::kLogout:
            return OperationAction::kLogout;
        case AuthAuditAction::kAuthFailed:
            return OperationAction::kAuthFailed;
        case AuthAuditAction::kTokenExpired:
            return OperationAction::kTokenExpired;
        case AuthAuditAction::kPermissionDenied:
            return OperationAction::kPermissionDenied;
    }
    return OperationAction::kAuthFailed;
}

OperationResult MapAuthResult(AuthAuditResult result) {
    switch (result) {
        case AuthAuditResult::kSuccess:
            return OperationResult::kSuccess;
        case AuthAuditResult::kFailed:
            return OperationResult::kFailed;
        case AuthAuditResult::kRejected:
            return OperationResult::kRejected;
    }
    return OperationResult::kFailed;
}

class AuthAuditToLoggerSink : public IAuthAuditSink {
public:
    explicit AuthAuditToLoggerSink(ILogger& logger) : logger_(logger) {}

    bool RecordAuthOperation(const AuthAuditRecord& record) override {
        OperationRecord operation;
        operation.request_id = record.context.request_id;
        operation.user_name = record.user_name;
        operation.session_id = record.session_id;
        operation.client_ip = record.context.client_ip;
        operation.module = record.module;
        operation.action = MapAuthAction(record.action);
        operation.target = record.target;
        operation.result = MapAuthResult(record.result);
        operation.reason = record.reason;
        return logger_.RecordOperation(operation);
    }

private:
    ILogger& logger_;
};

}  // namespace

FoundationSubsystem& FoundationSubsystem::Get() {
    static FoundationSubsystem subsystem;
    return subsystem;
}

bool FoundationSubsystem::Start(const StartupPaths& paths) {
    if (started_) {
        return true;
    }
    if (paths.business_config_path == nullptr ||
        paths.default_config_path == nullptr ||
        paths.auth_users_path == nullptr || paths.operation_log_path == nullptr) {
        Error("app", "Foundation subsystem paths are incomplete");
        return false;
    }

    LoggerConfig logger_config;
    logger_config.operation_log_path = paths.operation_log_path;
    if (IsProductionDataPath(paths.operation_log_path)) {
        logger_config.max_file_size_bytes = kProductionOperationLogMaxBytes;
        logger_config.max_rotate_files = kProductionOperationLogRotateFiles;
    }
    logger_ = CreateLogger(logger_config);
    if (!logger_ || !logger_->Start()) {
        Error("app", "Start logger failed: path=%s",
              paths.operation_log_path);
        Stop();
        return false;
    }

    ConfigOptions config_options;
    config_options.config_path = paths.business_config_path;
    config_options.default_config_path = paths.default_config_path;
    config_options.create_storage_if_missing = false;
    config_ = CreateConfig(config_options);
    if (!config_ || !config_->Start()) {
        Error("app", "Start config failed: config=%s default=%s",
              paths.business_config_path, paths.default_config_path);
        Stop();
        return false;
    }
    event_.reset(new event::Bus());
    if (!event_ || !event_->Start()) {
        Error("app", "Start event failed");
        Stop();
        return false;
    }

    AuthOptions auth_options;
    auth_options.token_ttl_seconds = 30 * 60;
    auth_options.max_sessions = 16;
    AuthDependencies auth_dependencies;
    auth_dependencies.config = config_.get();
    AuthUsersOptions auth_users_options;
    auth_users_options.kind = AuthUsersKind::kConfig;
    auth_users_options.config_path = paths.auth_users_path;
    auth_ = CreateAuth(auth_options, auth_dependencies,
                       CreateAuthUsers(auth_users_options),
                       CreatePasswordVerifier(PasswordVerifierKind::kPbkdf2));
    if (!auth_) {
        Error("app", "Create auth failed: users=%s",
              paths.auth_users_path);
        Stop();
        return false;
    }
    auth_audit_sink_.reset(new AuthAuditToLoggerSink(*logger_));
    (void)auth_->SetAuditSink(auth_audit_sink_.get());
    if (!auth_->Start()) {
        Error("app", "Start auth failed: users=%s",
              paths.auth_users_path);
        Stop();
        return false;
    }
    if (!Runtime::InstallLogger(logger_.get()) ||
        !Runtime::InstallConfig(config_.get()) ||
        !Runtime::InstallEvent(event_->dispatcher()) ||
        !Runtime::InstallAuth(auth_.get())) {
        Error("app", "Install runtime foundation services failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void FoundationSubsystem::Stop() {
    Runtime::Clear();
    if (auth_) {
        auth_->Stop();
        auth_.reset();
    }
    auth_audit_sink_.reset();
    if (event_) {
        event_->Stop(event::StopMode::kDiscard);
        event_.reset();
    }
    if (config_) {
        config_->Stop();
        config_.reset();
    }
    if (logger_) {
        logger_->Stop();
        logger_.reset();
    }
    started_ = false;
}

}  // namespace live_stream
