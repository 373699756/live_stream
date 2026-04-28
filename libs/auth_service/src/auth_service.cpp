#include "auth_internal.h"

#include "config_service.h"
#include "infra/sync.h"
#include "infra/time.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace live_stream {
namespace {

constexpr uint32_t kTokenBytes = 32;

struct FailureRecord {
    uint32_t failures = 0;
    int64_t locked_until_ms = 0;
};

std::string BytesToHex(const uint8_t* data, uint32_t size) {
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    hex.resize(static_cast<std::size_t>(size) * 2);
    for (uint32_t i = 0; i < size; ++i) {
        hex[static_cast<std::size_t>(i) * 2] = kHex[(data[i] >> 4) & 0x0F];
        hex[static_cast<std::size_t>(i) * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return hex;
}

infra::Result<std::string> SystemRandomToken() {
    uint8_t bytes[kTokenBytes] = {0};
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return infra::Result<std::string>::Fail(infra::Status::kIoError);
    }
    uint32_t offset = 0;
    while (offset < kTokenBytes) {
        const ssize_t n =
            read(fd, bytes + offset,
                 static_cast<std::size_t>(kTokenBytes - offset));
        if (n <= 0) {
            close(fd);
            return infra::Result<std::string>::Fail(infra::Status::kIoError);
        }
        offset += static_cast<uint32_t>(n);
    }
    close(fd);
    return infra::Result<std::string>::Ok(BytesToHex(bytes, kTokenBytes));
}

bool IsValidPolicy(const AuthServiceOptions& options) {
    return options.token_ttl_seconds > 0 && options.max_sessions > 0 &&
           options.max_sessions_per_user > 0 &&
           options.password_min_length <= auth_internal::kMaxPasswordLength;
}

bool ReadOptionalUint32(const ConfigJson& object,
                        const char* key,
                        uint32_t* value) {
    if (value == nullptr) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    uint64_t parsed = 0;
    const ConfigJson& field = object[key];
    if (field.is_number_unsigned()) {
        parsed = field.get<uint64_t>();
    } else if (field.is_number_integer()) {
        const int64_t signed_value = field.get<int64_t>();
        if (signed_value < 0) {
            return false;
        }
        parsed = static_cast<uint64_t>(signed_value);
    } else {
        return false;
    }
    if (parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool ReadOptionalBool(const ConfigJson& object,
                      const char* key,
                      bool* value) {
    if (value == nullptr) {
        return false;
    }
    if (!object.contains(key)) {
        return true;
    }
    const ConfigJson& field = object[key];
    if (!field.is_boolean()) {
        return false;
    }
    *value = field.get<bool>();
    return true;
}

infra::Result<AuthServiceOptions> ParseUserConfig(
    const ConfigJson& value,
    const AuthServiceOptions& fallback) {
    if (!value.is_object()) {
        return infra::Result<AuthServiceOptions>::Fail(
            infra::Status::kInvalidParam);
    }

    AuthServiceOptions options = fallback;
    if (value.contains("password_policy")) {
        const ConfigJson& policy = value["password_policy"];
        if (!policy.is_object()) {
            return infra::Result<AuthServiceOptions>::Fail(
                infra::Status::kInvalidParam);
        }
        if (!ReadOptionalUint32(policy, "min_length",
                                &options.password_min_length) ||
            !ReadOptionalBool(policy, "require_number",
                              &options.password_require_number) ||
            !ReadOptionalBool(policy, "require_symbol",
                              &options.password_require_symbol) ||
            !ReadOptionalUint32(policy, "lockout_failures",
                                &options.lockout_failures) ||
            !ReadOptionalUint32(policy, "lockout_seconds",
                                &options.lockout_seconds)) {
            return infra::Result<AuthServiceOptions>::Fail(
                infra::Status::kInvalidParam);
        }
    }
    if (value.contains("session")) {
        const ConfigJson& session = value["session"];
        if (!session.is_object()) {
            return infra::Result<AuthServiceOptions>::Fail(
                infra::Status::kInvalidParam);
        }
        if (!ReadOptionalUint32(session, "token_ttl_seconds",
                                &options.token_ttl_seconds) ||
            !ReadOptionalUint32(session, "max_sessions_per_user",
                                &options.max_sessions_per_user) ||
            options.token_ttl_seconds == 0 ||
            options.max_sessions_per_user == 0) {
            return infra::Result<AuthServiceOptions>::Fail(
                infra::Status::kInvalidParam);
        }
    }
    if (!IsValidPolicy(options)) {
        return infra::Result<AuthServiceOptions>::Fail(
            infra::Status::kInvalidParam);
    }
    return infra::Result<AuthServiceOptions>::Ok(options);
}

class AuthServiceImpl : public IAuthService {
 public:
    AuthServiceImpl(const AuthServiceOptions& options,
                    const AuthServiceDependencies& dependencies,
                    std::unique_ptr<IAuthUserStore> user_store,
                    std::unique_ptr<IPasswordVerifier> password_verifier,
                    IAuthTokenGenerator* token_generator)
        : options_(options),
          dependencies_(dependencies),
          user_store_(std::move(user_store)),
          password_verifier_(std::move(password_verifier)),
          token_generator_(token_generator) {}

    infra::Status Init() override {
        infra::MutexGuard guard(&mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (user_store_ == nullptr || password_verifier_ == nullptr ||
            !IsValidPolicy(options_)) {
            return infra::Status::kInvalidParam;
        }
        if (dependencies_.config_service != nullptr) {
            ConfigJson user_config;
            if (dependencies_.config_service->GetValue("user", &user_config) ==
                infra::Status::kOk) {
                const infra::Status status = ApplyConfigLocked(user_config);
                if (status != infra::Status::kOk) {
                    return status;
                }
            }
            if (!config_callbacks_registered_) {
                infra::Status status =
                    dependencies_.config_service->RegisterVerify(
                        "user", [this](const ConfigJson& value) {
                            return VerifyConfig(value);
                        });
                if (status != infra::Status::kOk) {
                    return status;
                }
                status = dependencies_.config_service->RegisterApply(
                    "user", [this](const ConfigJson& value) {
                        infra::MutexGuard apply_guard(&mutex_);
                        return ApplyConfigLocked(value);
                    });
                if (status != infra::Status::kOk) {
                    return status;
                }
                config_callbacks_registered_ = true;
            }
        }
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        infra::MutexGuard guard(&mutex_);
        if (!initialized_) {
            return infra::Status::kBusy;
        }
        started_ = true;
        return infra::Status::kOk;
    }

    void Stop() override {
        infra::MutexGuard guard(&mutex_);
        started_ = false;
    }

    void Deinit() override {
        infra::MutexGuard guard(&mutex_);
        sessions_.clear();
        failures_.clear();
        initialized_ = false;
        started_ = false;
    }

    const char* Name() const override { return "auth_service"; }

    infra::Status SetAuditSink(IAuthAuditSink* sink) override {
        infra::MutexGuard guard(&mutex_);
        audit_sink_ = sink;
        return infra::Status::kOk;
    }

    infra::Result<LoginResult> Login(const LoginRequest& request) override {
        if (auth_internal::IsEmptyOrTooLong(
                request.user_name, auth_internal::kMaxUserNameLength) ||
            request.password.size() > auth_internal::kMaxPasswordLength) {
            RecordLoginFailure(request.context, request.user_name,
                               "invalid_request");
            return infra::Result<LoginResult>::Fail(
                infra::Status::kInvalidParam);
        }
        if (!IsStarted()) {
            return infra::Result<LoginResult>::Fail(infra::Status::kBusy);
        }

        infra::Status lock_status = CheckLockout(request.user_name);
        if (lock_status != infra::Status::kOk) {
            RecordAudit(request.context, request.user_name, "",
                        AuthAuditAction::kAuthFailed, "login",
                        AuthAuditResult::kRejected, "locked");
            return infra::Result<LoginResult>::Fail(lock_status);
        }

        infra::Result<AuthUserRecord> user =
            user_store_->FindUser(request.user_name);
        if (!user.IsOk() || !user.value.enabled) {
            RegisterFailedAttempt(request.user_name);
            RecordLoginFailure(request.context, request.user_name,
                               user.IsOk() ? "disabled_user"
                                           : "user_not_found");
            return infra::Result<LoginResult>::Fail(
                infra::Status::kUnauthorized);
        }

        const infra::Status password_error =
            password_verifier_->VerifyPassword(request.password,
                                               user.value.password_credential);
        if (password_error != infra::Status::kOk) {
            RegisterFailedAttempt(request.user_name);
            RecordLoginFailure(request.context, request.user_name,
                               "bad_password");
            return infra::Result<LoginResult>::Fail(
                infra::Status::kUnauthorized);
        }

        LoginResult result;
        const infra::Status create_error = CreateSession(user.value, &result);
        if (create_error != infra::Status::kOk) {
            RecordLoginFailure(request.context, request.user_name,
                               "session_limit");
            return infra::Result<LoginResult>::Fail(create_error);
        }

        ClearFailedAttempts(request.user_name);
        {
            infra::MutexGuard guard(&mutex_);
            ++stats_.login_success;
        }

        infra::RequestContext audit_context = request.context;
        audit_context.user_name = result.principal.user_name;
        audit_context.session_id = result.principal.session_id;
        RecordAudit(audit_context, result.principal.user_name,
                    result.principal.session_id, AuthAuditAction::kLogin,
                    "session", AuthAuditResult::kSuccess, "");
        return infra::Result<LoginResult>::Ok(result);
    }

    infra::Status Logout(const infra::RequestContext& context) override {
        if (context.session_id.empty() && context.user_name.empty()) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }

        auth_internal::SessionRecord removed;
        bool found = false;
        {
            infra::MutexGuard guard(&mutex_);
            for (auto iter = sessions_.begin(); iter != sessions_.end();
                 ++iter) {
                const bool session_match =
                    !context.session_id.empty() &&
                    iter->second.principal.session_id == context.session_id;
                const bool user_match =
                    !context.user_name.empty() &&
                    iter->second.principal.user_name == context.user_name;
                if (session_match || user_match) {
                    removed = iter->second;
                    sessions_.erase(iter);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return infra::Status::kNotFound;
        }

        RecordAudit(context, removed.principal.user_name,
                    removed.principal.session_id, AuthAuditAction::kLogout,
                    "session", AuthAuditResult::kSuccess, "");
        return infra::Status::kOk;
    }

    infra::Result<TokenValidationResult> ValidateToken(
        const std::string& token) override {
        if (auth_internal::IsEmptyOrTooLong(
                token, auth_internal::kMaxTokenLength)) {
            return infra::Result<TokenValidationResult>::Fail(
                infra::Status::kInvalidParam);
        }
        if (!IsStarted()) {
            return infra::Result<TokenValidationResult>::Fail(
                infra::Status::kBusy);
        }

        auth_internal::SessionRecord expired;
        bool found_expired = false;
        {
            infra::MutexGuard guard(&mutex_);
            const auto iter = sessions_.find(token);
            if (iter == sessions_.end()) {
                ++stats_.token_validation_failed;
                return infra::Result<TokenValidationResult>::Fail(
                    infra::Status::kUnauthorized);
            }
            if (iter->second.expires_at_monotonic_ms <=
                infra::Time::MonotonicMillis()) {
                expired = iter->second;
                sessions_.erase(iter);
                ++stats_.token_validation_failed;
                ++stats_.expired_sessions;
                found_expired = true;
            } else {
                TokenValidationResult result;
                result.principal = iter->second.principal;
                result.expires_at_ms = iter->second.expires_at_ms;
                return infra::Result<TokenValidationResult>::Ok(result);
            }
        }

        if (found_expired) {
            infra::RequestContext context;
            context.user_name = expired.principal.user_name;
            context.session_id = expired.principal.session_id;
            RecordAudit(context, expired.principal.user_name,
                        expired.principal.session_id,
                        AuthAuditAction::kTokenExpired, "session",
                        AuthAuditResult::kRejected, "token_expired");
        }
        return infra::Result<TokenValidationResult>::Fail(
            infra::Status::kUnauthorized);
    }

    infra::Status CheckPermission(
        const AuthPrincipal& principal,
        AuthPermission permission,
        const std::string& target) override {
        if (auth_internal::IsEmptyOrTooLong(
                principal.user_name, auth_internal::kMaxUserNameLength) ||
            auth_internal::IsEmptyOrTooLong(
                principal.session_id, auth_internal::kMaxTokenLength) ||
            target.size() > auth_internal::kMaxTargetLength) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStarted()) {
            return infra::Status::kBusy;
        }
        if (auth_internal::RoleHasPermission(principal.role, permission)) {
            return infra::Status::kOk;
        }

        infra::RequestContext context;
        context.user_name = principal.user_name;
        context.session_id = principal.session_id;
        RecordAudit(context, principal.user_name, principal.session_id,
                    AuthAuditAction::kPermissionDenied,
                    target.empty() ? AuthPermissionToString(permission)
                                   : target,
                    AuthAuditResult::kRejected, "permission_denied");
        return infra::Status::kNoPermission;
    }

    AuthStats GetStats() const override {
        infra::MutexGuard guard(&mutex_);
        AuthStats stats = stats_;
        stats.active_sessions = static_cast<uint32_t>(sessions_.size());
        return stats;
    }

 private:
    infra::Status VerifyConfig(const ConfigJson& value) const {
        infra::MutexGuard guard(&mutex_);
        return ParseUserConfig(value, options_).status;
    }

    infra::Status ApplyConfigLocked(const ConfigJson& value) {
        infra::Result<AuthServiceOptions> parsed =
            ParseUserConfig(value, options_);
        if (!parsed.IsOk()) {
            ++stats_.config_apply_failed_count;
            return parsed.status;
        }
        options_ = parsed.value;
        PruneExpiredSessionsLocked();
        EnforcePerUserSessionLimitLocked();
        ++stats_.config_apply_count;
        return infra::Status::kOk;
    }

    bool IsStarted() {
        infra::MutexGuard guard(&mutex_);
        return initialized_ && started_;
    }

    infra::Status CheckLockout(const std::string& user_name) {
        infra::MutexGuard guard(&mutex_);
        const auto iter = failures_.find(user_name);
        if (iter == failures_.end()) {
            return infra::Status::kOk;
        }
        const int64_t now = infra::Time::MonotonicMillis();
        if (iter->second.locked_until_ms > now) {
            return infra::Status::kBusy;
        }
        if (iter->second.locked_until_ms != 0) {
            failures_.erase(iter);
        }
        return infra::Status::kOk;
    }

    void RegisterFailedAttempt(const std::string& user_name) {
        infra::MutexGuard guard(&mutex_);
        ++stats_.login_failed;
        if (options_.lockout_failures == 0 || options_.lockout_seconds == 0) {
            return;
        }
        FailureRecord& failure = failures_[user_name];
        const int64_t now = infra::Time::MonotonicMillis();
        if (failure.locked_until_ms > now) {
            return;
        }
        ++failure.failures;
        if (failure.failures >= options_.lockout_failures) {
            failure.locked_until_ms =
                now + static_cast<int64_t>(options_.lockout_seconds) * 1000;
            failure.failures = 0;
            ++stats_.lockout_count;
        }
    }

    void ClearFailedAttempts(const std::string& user_name) {
        infra::MutexGuard guard(&mutex_);
        failures_.erase(user_name);
    }

    void RecordLoginFailure(const infra::RequestContext& context,
                            const std::string& user_name,
                            const std::string& reason) {
        RecordAudit(context, user_name, "", AuthAuditAction::kAuthFailed,
                    "login", AuthAuditResult::kRejected, reason);
    }

    infra::Status CreateSession(const AuthUserRecord& user,
                                LoginResult* result) {
        if (result == nullptr) {
            return infra::Status::kInvalidParam;
        }

        infra::Result<std::string> token =
            token_generator_ != nullptr ? token_generator_->GenerateToken()
                                        : SystemRandomToken();
        if (!token.IsOk()) {
            return token.status;
        }

        infra::MutexGuard guard(&mutex_);
        PruneExpiredSessionsLocked();
        EnforceUserSessionLimitLocked(user.user_name);
        if (sessions_.size() >= options_.max_sessions) {
            return infra::Status::kBusy;
        }
        while (sessions_.find(token.value) != sessions_.end()) {
            token = token_generator_ != nullptr ? token_generator_->GenerateToken()
                                                : SystemRandomToken();
            if (!token.IsOk()) {
                return token.status;
            }
        }

        ++sequence_;
        const int64_t now = infra::Time::MonotonicMillis();
        const int64_t wall_now = infra::Time::SystemTimeMillis();
        const int64_t ttl_ms =
            static_cast<int64_t>(options_.token_ttl_seconds) * 1000;

        auth_internal::SessionRecord session;
        session.principal.user_name = user.user_name;
        session.principal.session_id = auth_internal::MakeSessionId(sequence_);
        session.principal.role = user.role;
        session.token = token.value;
        session.created_at_monotonic_ms = now;
        session.expires_at_monotonic_ms = now + ttl_ms;
        session.expires_at_ms = wall_now + ttl_ms;

        sessions_[token.value] = session;
        result->principal = session.principal;
        result->token = token.value;
        result->expires_at_ms = session.expires_at_ms;
        return infra::Status::kOk;
    }

    void PruneExpiredSessionsLocked() {
        const int64_t now = infra::Time::MonotonicMillis();
        for (auto iter = sessions_.begin(); iter != sessions_.end();) {
            if (iter->second.expires_at_monotonic_ms <= now) {
                iter = sessions_.erase(iter);
                ++stats_.expired_sessions;
            } else {
                ++iter;
            }
        }
    }

    uint32_t CountUserSessionsLocked(const std::string& user_name) const {
        uint32_t count = 0;
        for (const auto& item : sessions_) {
            if (item.second.principal.user_name == user_name) {
                ++count;
            }
        }
        return count;
    }

    bool RemoveOldestUserSessionLocked(const std::string& user_name) {
        auto oldest = sessions_.end();
        for (auto iter = sessions_.begin(); iter != sessions_.end(); ++iter) {
            if (iter->second.principal.user_name != user_name) {
                continue;
            }
            if (oldest == sessions_.end() ||
                iter->second.created_at_monotonic_ms <
                    oldest->second.created_at_monotonic_ms) {
                oldest = iter;
            }
        }
        if (oldest == sessions_.end()) {
            return false;
        }
        sessions_.erase(oldest);
        return true;
    }

    void EnforceUserSessionLimitLocked(const std::string& user_name) {
        while (CountUserSessionsLocked(user_name) >=
               options_.max_sessions_per_user) {
            if (!RemoveOldestUserSessionLocked(user_name)) {
                return;
            }
        }
    }

    void EnforcePerUserSessionLimitLocked() {
        std::map<std::string, bool> seen;
        for (const auto& item : sessions_) {
            seen[item.second.principal.user_name] = true;
        }
        for (const auto& user : seen) {
            while (CountUserSessionsLocked(user.first) >
                   options_.max_sessions_per_user) {
                if (!RemoveOldestUserSessionLocked(user.first)) {
                    break;
                }
            }
        }
    }

    void RecordAudit(const infra::RequestContext& context,
                     const std::string& user_name,
                     const std::string& session_id,
                     AuthAuditAction action,
                     const std::string& target,
                     AuthAuditResult result,
                     const std::string& reason) {
        IAuthAuditSink* sink = nullptr;
        {
            infra::MutexGuard guard(&mutex_);
            sink = audit_sink_;
        }
        if (sink == nullptr) {
            return;
        }

        AuthAuditRecord record;
        record.context = context;
        record.user_name = user_name;
        record.session_id = session_id;
        record.module = "auth_service";
        record.action = action;
        record.target = target;
        record.result = result;
        record.reason = reason;
        static_cast<void>(sink->RecordAuthOperation(record));
    }

    AuthServiceOptions options_;
    AuthServiceDependencies dependencies_;
    std::unique_ptr<IAuthUserStore> user_store_;
    std::unique_ptr<IPasswordVerifier> password_verifier_;
    IAuthTokenGenerator* token_generator_ = nullptr;
    mutable infra::Mutex mutex_;
    std::map<std::string, auth_internal::SessionRecord> sessions_;
    std::map<std::string, FailureRecord> failures_;
    IAuthAuditSink* audit_sink_ = nullptr;
    AuthStats stats_;
    uint64_t sequence_ = 0;
    bool config_callbacks_registered_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IAuthService> CreateAuthService(
    const AuthServiceOptions& options,
    std::unique_ptr<IAuthUserStore> user_store,
    std::unique_ptr<IPasswordVerifier> password_verifier) {
    return CreateAuthService(options, AuthServiceDependencies{},
                             std::move(user_store),
                             std::move(password_verifier), nullptr);
}

std::unique_ptr<IAuthService> CreateAuthService(
    const AuthServiceOptions& options,
    const AuthServiceDependencies& dependencies,
    std::unique_ptr<IAuthUserStore> user_store,
    std::unique_ptr<IPasswordVerifier> password_verifier,
    IAuthTokenGenerator* token_generator) {
    return std::unique_ptr<IAuthService>(
        new AuthServiceImpl(options, dependencies, std::move(user_store),
                            std::move(password_verifier), token_generator));
}

}  // namespace live_stream
