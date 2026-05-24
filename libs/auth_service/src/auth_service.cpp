#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "auth_internal.h"
#include "config_service.h"
#include "infra/time.h"
#include "json_utils.h"

namespace live_stream {
namespace {

constexpr uint32_t kSessionIdBytes = 16;
constexpr uint32_t kTokenBytes = 32;
constexpr uint32_t kPasswordSaltBytes = 16;
constexpr uint32_t kPasswordPbkdf2Iterations = 100000;

struct FailureRecord {
    uint32_t failures = 0;
    int64_t locked_until_ms = 0;
};

std::string BytesToHex(const uint8_t *data, uint32_t size) {
    static const char *kHex = "0123456789abcdef";
    std::string hex;
    hex.resize(static_cast<std::size_t>(size) * 2);
    for (uint32_t i = 0; i < size; ++i) {
        hex[static_cast<std::size_t>(i) * 2] = kHex[(data[i] >> 4) & 0x0F];
        hex[static_cast<std::size_t>(i) * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return hex;
}

std::string SystemRandomHex(uint32_t byte_count) {
    uint8_t bytes[kTokenBytes] = {0};
    if (byte_count == 0 || byte_count > kTokenBytes) {
        return std::string();
    }
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::string();
    }
    uint32_t offset = 0;
    while (offset < byte_count) {
        const ssize_t n = read(fd, bytes + offset,
                               static_cast<std::size_t>(byte_count - offset));
        if (n <= 0) {
            close(fd);
            return std::string();
        }
        offset += static_cast<uint32_t>(n);
    }
    close(fd);
    return BytesToHex(bytes, byte_count);
}

std::string SystemRandomToken() {
    return SystemRandomHex(kTokenBytes);
}

std::string SystemRandomSessionId() {
    const std::string random = SystemRandomHex(kSessionIdBytes);
    if (random.empty()) {
        return std::string();
    }
    return std::string("auth-") + random;
}

bool IsValidPolicy(const AuthServiceOptions &options) {
    return options.token_ttl_seconds > 0 && options.max_sessions > 0 &&
           options.max_sessions_per_user > 0 &&
           options.password_min_length <= auth_internal::kMaxPasswordLength;
}

bool HasDigit(const std::string &value) {
    for (char ch : value) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            return true;
        }
    }
    return false;
}

bool HasSymbol(const std::string &value) {
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0) {
            return true;
        }
    }
    return false;
}

bool IsValidNewPassword(const std::string &password,
                        const AuthServiceOptions &options) {
    if (password.empty() ||
        password.size() > auth_internal::kMaxPasswordLength ||
        password.size() < options.password_min_length) {
        return false;
    }
    if (options.password_require_number && !HasDigit(password)) {
        return false;
    }
    if (options.password_require_symbol && !HasSymbol(password)) {
        return false;
    }
    return true;
}

std::string GeneratePasswordCredential(const std::string &password) {
    const std::string salt_hex = SystemRandomHex(kPasswordSaltBytes);
    if (salt_hex.empty()) {
        return std::string();
    }
    return auth_internal::Pbkdf2Sha256Credential(
        password, salt_hex, kPasswordPbkdf2Iterations);
}

bool ParseUserConfig(const ConfigJson &value,
                     const AuthServiceOptions &fallback,
                     AuthServiceOptions *parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (!value.is_object()) {
        return false;
    }

    AuthServiceOptions options = fallback;
    if (!value.contains("password_policy") ||
        !value.at("password_policy").is_object()) {
        return false;
    }
    const ConfigJson &policy = value.at("password_policy");
    if (!json_utils::ReadField(policy, "min_length", &options.password_min_length, 0,
                          std::numeric_limits<uint32_t>::max()) ||
        !json_utils::ReadField(policy, "require_number",
                          &options.password_require_number) ||
        !json_utils::ReadField(policy, "require_symbol",
                          &options.password_require_symbol) ||
        !json_utils::ReadField(policy, "lockout_failures", &options.lockout_failures,
                          0, std::numeric_limits<uint32_t>::max()) ||
        !json_utils::ReadField(policy, "lockout_seconds", &options.lockout_seconds, 0,
                          std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    if (!value.contains("session") || !value.at("session").is_object()) {
        return false;
    }
    const ConfigJson &session = value.at("session");
    if (!json_utils::ReadField(session, "token_ttl_seconds",
                          &options.token_ttl_seconds, 1,
                          std::numeric_limits<uint32_t>::max()) ||
        !json_utils::ReadField(session, "max_sessions_per_user",
                          &options.max_sessions_per_user, 1,
                          std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    if (!IsValidPolicy(options)) {
        return false;
    }
    *parsed = options;
    return true;
}

class AuthServiceImpl : public IAuthService {
public:
    AuthServiceImpl(const AuthServiceOptions &options,
                    const AuthServiceDependencies &dependencies,
                    std::unique_ptr<IAuthUserStore> user_store,
                    std::unique_ptr<IPasswordVerifier> password_verifier,
                    IAuthTokenGenerator *token_generator)
        : options_(options), dependencies_(dependencies), user_store_(std::move(user_store)), password_verifier_(std::move(password_verifier)), token_generator_(token_generator) {}

    bool Prepare() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (initialized_) {
            return true;
        }
        if (user_store_ == nullptr || password_verifier_ == nullptr ||
            !IsValidPolicy(options_)) {
            return false;
        }
        if (dependencies_.config_service != nullptr) {
            ConfigJson user_config = dependencies_.config_service->GetValue("user");
            if (user_config.is_object()) {
                if (!ApplyConfigLocked(user_config)) {
                    return false;
                }
            }
            if (!config_attached_) {
                ConfigAttachment attachment;
                attachment.validate = [this](const ConfigJson &value) {
                    return VerifyConfig(value)
                               ? ConfigResult::Success()
                               : ConfigResult::Failure("", "invalid user config");
                };
                attachment.apply = [this](const ConfigJson &value) {
                    std::lock_guard<std::mutex> apply_guard(mutex_);
                    return ApplyConfigLocked(value)
                               ? ConfigResult::Success()
                               : ConfigResult::Failure("", "apply user config failed");
                };
                if (!dependencies_.config_service->AttachConfig("user", attachment)) {
                    return false;
                }
                config_attached_ = true;
            }
        }
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        started_ = true;
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> guard(mutex_);
        started_ = false;
    }

    bool IsStarted() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        return initialized_ && started_;
    }

    void Release() {
        std::lock_guard<std::mutex> guard(mutex_);
        sessions_.clear();
        failures_.clear();
        initialized_ = false;
        started_ = false;
    }

    bool SetAuditSink(IAuthAuditSink *sink) override {
        std::lock_guard<std::mutex> guard(mutex_);
        audit_sink_ = sink;
        return true;
    }

    LoginResult Login(const LoginRequest &request) override {
        if (auth_internal::IsEmptyOrTooLong(request.user_name,
                                            auth_internal::kMaxUserNameLength) ||
            request.password.size() > auth_internal::kMaxPasswordLength) {
            RecordLoginFailure(request.context, request.user_name, "invalid_request");
            return LoginResult{};
        }
        if (!IsStarted()) {
            return LoginResult{};
        }

        if (!CheckLockout(request.user_name)) {
            RecordAudit(request.context, request.user_name, "",
                        AuthAuditAction::kAuthFailed, "login",
                        AuthAuditResult::kRejected, "locked");
            return LoginResult{};
        }

        AuthUserRecord user = user_store_->FindUser(request.user_name);
        if (user.user_name.empty() || !user.enabled) {
            RegisterFailedAttempt(request.user_name);
            RecordLoginFailure(request.context, request.user_name,
                               user.user_name.empty() ? "user_not_found"
                                                      : "disabled_user");
            return LoginResult{};
        }

        if (!password_verifier_->VerifyPassword(request.password,
                                                user.password_credential)) {
            RegisterFailedAttempt(request.user_name);
            RecordLoginFailure(request.context, request.user_name, "bad_password");
            return LoginResult{};
        }

        LoginResult result;
        if (!CreateSession(user, &result)) {
            RecordLoginFailure(request.context, request.user_name, "session_limit");
            return LoginResult{};
        }

        ClearFailedAttempts(request.user_name);
        {
            std::lock_guard<std::mutex> guard(mutex_);
            ++stats_.login_success;
        }

        live_stream::RequestContext audit_context = request.context;
        audit_context.user_name = result.principal.user_name;
        audit_context.session_id = result.principal.session_id;
        RecordAudit(audit_context, result.principal.user_name,
                    result.principal.session_id, AuthAuditAction::kLogin, "session",
                    AuthAuditResult::kSuccess, "");
        return result;
    }

    bool Logout(const live_stream::RequestContext &context) override {
        if (context.session_id.empty()) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }

        auth_internal::SessionRecord removed;
        bool found = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            for (auto iter = sessions_.begin(); iter != sessions_.end(); ++iter) {
                const bool session_match =
                    iter->second.principal.session_id == context.session_id;
                if (session_match) {
                    removed = iter->second;
                    sessions_.erase(iter);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }

        RecordAudit(context, removed.principal.user_name,
                    removed.principal.session_id, AuthAuditAction::kLogout,
                    "session", AuthAuditResult::kSuccess, "");
        return true;
    }

    TokenValidationResult ValidateToken(const std::string &token) override {
        if (auth_internal::IsEmptyOrTooLong(token,
                                            auth_internal::kMaxTokenLength)) {
            return TokenValidationResult{};
        }
        if (!IsStarted()) {
            return TokenValidationResult{};
        }

        auth_internal::SessionRecord expired;
        bool found_expired = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const auto iter = sessions_.find(token);
            if (iter == sessions_.end()) {
                ++stats_.token_validation_failed;
                return TokenValidationResult{};
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
                result.must_change_password =
                    iter->second.must_change_password;
                return result;
            }
        }

        if (found_expired) {
            live_stream::RequestContext context;
            context.user_name = expired.principal.user_name;
            context.session_id = expired.principal.session_id;
            RecordAudit(context, expired.principal.user_name,
                        expired.principal.session_id, AuthAuditAction::kTokenExpired,
                        "session", AuthAuditResult::kRejected, "token_expired");
        }
        return TokenValidationResult{};
    }

    bool ChangePassword(const ChangePasswordRequest &request) override {
        if (auth_internal::IsEmptyOrTooLong(request.context.user_name,
                                            auth_internal::kMaxUserNameLength) ||
            auth_internal::IsEmptyOrTooLong(request.context.session_id,
                                            auth_internal::kMaxTokenLength) ||
            request.old_password.size() > auth_internal::kMaxPasswordLength) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }
        AuthServiceOptions options_snapshot;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            options_snapshot = options_;
        }
        if (!IsValidNewPassword(request.new_password, options_snapshot)) {
            return false;
        }
        if (!HasActiveSession(request.context)) {
            return false;
        }

        AuthUserRecord user = user_store_->FindUser(request.context.user_name);
        if (user.user_name.empty() || !user.enabled) {
            return false;
        }
        if (!password_verifier_->VerifyPassword(request.old_password,
                                                user.password_credential)) {
            RecordLoginFailure(request.context, request.context.user_name,
                               "bad_password");
            return false;
        }

        const std::string credential =
            GeneratePasswordCredential(request.new_password);
        if (credential.empty()) {
            return false;
        }
        if (!user_store_->UpdatePassword(user.user_name, credential, false)) {
            return false;
        }
        AcceptChangedPasswordSession(request.context);
        return true;
    }

    bool CheckPermission(const AuthPrincipal &principal,
                         AuthPermission permission,
                         const std::string &target) override {
        if (auth_internal::IsEmptyOrTooLong(principal.user_name,
                                            auth_internal::kMaxUserNameLength) ||
            auth_internal::IsEmptyOrTooLong(principal.session_id,
                                            auth_internal::kMaxTokenLength) ||
            target.size() > auth_internal::kMaxTargetLength) {
            return false;
        }
        if (!IsStarted()) {
            return false;
        }
        if (principal.must_change_password) {
            live_stream::RequestContext context;
            context.user_name = principal.user_name;
            context.session_id = principal.session_id;
            RecordAudit(context, principal.user_name, principal.session_id,
                        AuthAuditAction::kPermissionDenied,
                        target.empty() ? AuthPermissionToString(permission)
                                       : target,
                        AuthAuditResult::kRejected,
                        "must_change_password");
            return false;
        }
        if (auth_internal::RoleHasPermission(principal.role, permission)) {
            return true;
        }

        live_stream::RequestContext context;
        context.user_name = principal.user_name;
        context.session_id = principal.session_id;
        RecordAudit(context, principal.user_name, principal.session_id,
                    AuthAuditAction::kPermissionDenied,
                    target.empty() ? AuthPermissionToString(permission) : target,
                    AuthAuditResult::kRejected, "permission_denied");
        return false;
    }

    AuthStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        AuthStats stats = stats_;
        stats.active_sessions = static_cast<uint32_t>(sessions_.size());
        return stats;
    }

private:
    bool VerifyConfig(const ConfigJson &value) const {
        std::lock_guard<std::mutex> guard(mutex_);
        AuthServiceOptions parsed;
        return ParseUserConfig(value, options_, &parsed);
    }

    bool ApplyConfigLocked(const ConfigJson &value) {
        AuthServiceOptions parsed;
        ParseUserConfig(value, options_, &parsed);
        options_ = parsed;
        PruneExpiredSessionsLocked();
        EnforcePerUserSessionLimitLocked();
        ++stats_.config_apply_count;
        return true;
    }

    bool CheckLockout(const std::string &user_name) {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto iter = failures_.find(user_name);
        if (iter == failures_.end()) {
            return true;
        }
        const int64_t now = infra::Time::MonotonicMillis();
        if (iter->second.locked_until_ms > now) {
            return false;
        }
        if (iter->second.locked_until_ms != 0) {
            failures_.erase(iter);
        }
        return true;
    }

    void RegisterFailedAttempt(const std::string &user_name) {
        std::lock_guard<std::mutex> guard(mutex_);
        ++stats_.login_failed;
        if (options_.lockout_failures == 0 || options_.lockout_seconds == 0) {
            return;
        }
        FailureRecord &failure = failures_[user_name];
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

    void ClearFailedAttempts(const std::string &user_name) {
        std::lock_guard<std::mutex> guard(mutex_);
        failures_.erase(user_name);
    }

    void RecordLoginFailure(const live_stream::RequestContext &context,
                            const std::string &user_name,
                            const std::string &reason) {
        RecordAudit(context, user_name, "", AuthAuditAction::kAuthFailed, "login",
                    AuthAuditResult::kRejected, reason);
    }

    bool CreateSession(const AuthUserRecord &user, LoginResult *result) {
        if (result == nullptr) {
            return false;
        }

        std::string token = token_generator_ != nullptr
                                ? token_generator_->GenerateToken()
                                : SystemRandomToken();
        if (token.empty()) {
            return false;
        }
        std::string session_id = SystemRandomSessionId();
        if (session_id.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> guard(mutex_);
        PruneExpiredSessionsLocked();
        EnforceUserSessionLimitLocked(user.user_name);
        if (sessions_.size() >= options_.max_sessions) {
            return false;
        }
        while (sessions_.find(token) != sessions_.end()) {
            token = token_generator_ != nullptr ? token_generator_->GenerateToken()
                                                : SystemRandomToken();
            if (token.empty()) {
                return false;
            }
        }
        while (ContainsSessionIdLocked(session_id)) {
            session_id = SystemRandomSessionId();
            if (session_id.empty()) {
                return false;
            }
        }

        const int64_t now = infra::Time::MonotonicMillis();
        const int64_t wall_now = infra::Time::SystemTimeMillis();
        const int64_t ttl_ms =
            static_cast<int64_t>(options_.token_ttl_seconds) * 1000;

        auth_internal::SessionRecord session;
        session.principal.user_name = user.user_name;
        session.principal.session_id = session_id;
        session.principal.role = user.role;
        session.principal.must_change_password = user.must_change_password;
        session.token = token;
        session.created_at_monotonic_ms = now;
        session.expires_at_monotonic_ms = now + ttl_ms;
        session.expires_at_ms = wall_now + ttl_ms;
        session.must_change_password = user.must_change_password;

        sessions_[token] = session;
        result->principal = session.principal;
        result->token = token;
        result->expires_at_ms = session.expires_at_ms;
        result->must_change_password = session.must_change_password;
        return true;
    }

    void AcceptChangedPasswordSession(
        const live_stream::RequestContext &context) {
        std::lock_guard<std::mutex> guard(mutex_);
        for (auto iter = sessions_.begin(); iter != sessions_.end();) {
            if (iter->second.principal.user_name != context.user_name) {
                ++iter;
                continue;
            }
            if (iter->second.principal.session_id == context.session_id) {
                iter->second.must_change_password = false;
                iter->second.principal.must_change_password = false;
                ++iter;
                continue;
            }
            iter = sessions_.erase(iter);
        }
    }

    bool ContainsSessionIdLocked(const std::string &session_id) const {
        for (const auto &item : sessions_) {
            if (item.second.principal.session_id == session_id) {
                return true;
            }
        }
        return false;
    }

    bool HasActiveSession(const live_stream::RequestContext &context) {
        std::lock_guard<std::mutex> guard(mutex_);
        PruneExpiredSessionsLocked();
        for (const auto &item : sessions_) {
            if (item.second.principal.user_name == context.user_name &&
                item.second.principal.session_id == context.session_id) {
                return true;
            }
        }
        return false;
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

    uint32_t CountUserSessionsLocked(const std::string &user_name) const {
        uint32_t count = 0;
        for (const auto &item : sessions_) {
            if (item.second.principal.user_name == user_name) {
                ++count;
            }
        }
        return count;
    }

    bool RemoveOldestUserSessionLocked(const std::string &user_name) {
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

    void EnforceUserSessionLimitLocked(const std::string &user_name) {
        while (CountUserSessionsLocked(user_name) >=
               options_.max_sessions_per_user) {
            if (!RemoveOldestUserSessionLocked(user_name)) {
                return;
            }
        }
    }

    void EnforcePerUserSessionLimitLocked() {
        std::map<std::string, bool> seen;
        for (const auto &item : sessions_) {
            seen[item.second.principal.user_name] = true;
        }
        for (const auto &user : seen) {
            while (CountUserSessionsLocked(user.first) >
                   options_.max_sessions_per_user) {
                if (!RemoveOldestUserSessionLocked(user.first)) {
                    break;
                }
            }
        }
    }

    void RecordAudit(const live_stream::RequestContext &context,
                     const std::string &user_name, const std::string &session_id,
                     AuthAuditAction action, const std::string &target,
                     AuthAuditResult result, const std::string &reason) {
        IAuthAuditSink *sink = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
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
    IAuthTokenGenerator *token_generator_ = nullptr;
    mutable std::mutex mutex_;
    std::map<std::string, auth_internal::SessionRecord> sessions_;
    std::map<std::string, FailureRecord> failures_;
    IAuthAuditSink *audit_sink_ = nullptr;
    AuthStats stats_;
    bool config_attached_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IAuthService>
CreateAuthService(const AuthServiceOptions &options,
                  std::unique_ptr<IAuthUserStore> user_store,
                  std::unique_ptr<IPasswordVerifier> password_verifier) {
    return CreateAuthService(options, AuthServiceDependencies{},
                             std::move(user_store), std::move(password_verifier),
                             nullptr);
}

std::unique_ptr<IAuthService>
CreateAuthService(const AuthServiceOptions &options,
                  const AuthServiceDependencies &dependencies,
                  std::unique_ptr<IAuthUserStore> user_store,
                  std::unique_ptr<IPasswordVerifier> password_verifier,
                  IAuthTokenGenerator *token_generator) {
    return std::unique_ptr<IAuthService>(
        new AuthServiceImpl(options, dependencies, std::move(user_store),
                            std::move(password_verifier), token_generator));
}

}  // namespace live_stream
