#include "subsystems/core_subsystem.h"

#include "infra/log.h"
#include "json_utils.h"

namespace live_stream {
namespace {

constexpr const char* kAudioConfigName = "audio";

ConfigResult ValidateAudioScopeConfig(const ConfigJson& value) {
    if (!value.is_object()) {
        return ConfigResult::Failure("", "invalid audio config");
    }
    bool enabled = false;
    if (!json_utils::ReadField(value, "enabled", &enabled)) {
        return ConfigResult::Failure("enabled", "missing or invalid value");
    }
    if (enabled) {
        return ConfigResult::Failure("enabled", "audio is not supported");
    }
    return ConfigResult::Success();
}

bool InstallProductScopeConfigGuards(IConfig* config) {
    if (config == nullptr) {
        return false;
    }
    const ConfigJson audio_config = config->GetValue(kAudioConfigName);
    if (!audio_config.is_null()) {
        const ConfigResult result = ValidateAudioScopeConfig(audio_config);
        if (!result.ok) {
            return false;
        }
    }

    ConfigAttachment audio_attachment;
    audio_attachment.validate = ValidateAudioScopeConfig;
    audio_attachment.apply = [](const ConfigJson&) {
        return ConfigResult::Success();
    };
    return config->AttachConfig(kAudioConfigName, audio_attachment);
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
    explicit AuthAuditToLoggerSink(ILogger* logger) : logger_(logger) {}

    bool RecordAuthOperation(const AuthAuditRecord& record) override {
        if (logger_ == nullptr) {
            return false;
        }

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
        return logger_->RecordOperation(operation);
    }

private:
    ILogger* logger_ = nullptr;
};

}  // namespace

CoreSubsystem& CoreSubsystem::Get() {
    static CoreSubsystem subsystem;
    return subsystem;
}

bool CoreSubsystem::Start(const RuntimePaths& paths) {
    if (started_) {
        return true;
    }
    if (paths.business_config_path == nullptr ||
        paths.default_config_path == nullptr ||
        paths.auth_users_path == nullptr || paths.operation_log_path == nullptr) {
        Error("app", "Core subsystem paths are incomplete");
        return false;
    }

    LoggerConfig logger_config;
    logger_config.operation_log_path = paths.operation_log_path;
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
    if (!InstallProductScopeConfigGuards(config_.get())) {
        Error("app", "Install product scope config guards failed");
        Stop();
        return false;
    }

    event_ = CreateEvent();
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
    AuthUserStoreOptions auth_user_store_options;
    auth_user_store_options.kind = AuthUserStoreKind::kConfig;
    auth_user_store_options.config_path = paths.auth_users_path;
    auth_ = CreateAuth(auth_options, auth_dependencies,
                       CreateAuthUserStore(auth_user_store_options),
                       CreatePasswordVerifier(PasswordVerifierKind::kPbkdf2));
    auth_audit_sink_.reset(new AuthAuditToLoggerSink(logger_.get()));
    if (auth_ != nullptr) {
        (void)auth_->SetAuditSink(auth_audit_sink_.get());
    }
    if (!auth_ || !auth_->Start()) {
        Error("app", "Start auth failed: users=%s",
                        paths.auth_users_path);
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void CoreSubsystem::Stop() {
    if (auth_) {
        auth_->Stop();
        auth_.reset();
    }
    auth_audit_sink_.reset();
    if (event_) {
        event_->Stop();
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
