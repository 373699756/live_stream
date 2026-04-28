#include "auth_internal.h"

#include "infra/time.h"
#include "infra/sync.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace live_stream {
namespace {

class AuthServiceImpl : public IAuthService {
 public:
    AuthServiceImpl(const AuthServiceOptions& options,
                    std::unique_ptr<IAuthUserStore> user_store,
                    std::unique_ptr<IPasswordVerifier> password_verifier)
        : options_(options),
          user_store_(std::move(user_store)),
          password_verifier_(std::move(password_verifier)) {}

    infra::Status Init() override {
        infra::MutexGuard guard(&mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (user_store_ == nullptr || password_verifier_ == nullptr ||
            options_.token_ttl_seconds == 0 || options_.max_sessions == 0) {
            return infra::Status::kInvalidParam;
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
            RecordAudit(request.context, request.user_name, "",
                        AuthAuditAction::kAuthFailed, "login",
                        AuthAuditResult::kRejected, "invalid login request");
            return infra::Result<LoginResult>::Fail(infra::Status::kInvalidParam);
        }
        if (!IsStarted()) {
            return infra::Result<LoginResult>::Fail(infra::Status::kBusy);
        }

        infra::Result<AuthUserRecord> user =
            user_store_->FindUser(request.user_name);
        if (!user.IsOk() || !user.value.enabled) {
            RecordAudit(request.context, request.user_name, "",
                        AuthAuditAction::kAuthFailed, "login",
                        AuthAuditResult::kRejected, "user not authorized");
            return infra::Result<LoginResult>::Fail(infra::Status::kUnauthorized);
        }

        const infra::Status password_error =
            password_verifier_->VerifyPassword(request.password,
                                               user.value.password_credential);
        if (password_error != infra::Status::kOk) {
            RecordAudit(request.context, request.user_name, "",
                        AuthAuditAction::kAuthFailed, "login",
                        AuthAuditResult::kRejected, "password mismatch");
            return infra::Result<LoginResult>::Fail(infra::Status::kUnauthorized);
        }

        LoginResult result;
        const infra::Status create_error = CreateSession(user.value, &result);
        if (create_error != infra::Status::kOk) {
            RecordAudit(request.context, request.user_name, "",
                        AuthAuditAction::kAuthFailed, "login",
                        AuthAuditResult::kFailed, "session limit reached");
            return infra::Result<LoginResult>::Fail(create_error);
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
            for (auto iter = sessions_.begin(); iter != sessions_.end(); ++iter) {
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
                return infra::Result<TokenValidationResult>::Fail(
                    infra::Status::kUnauthorized);
            }
            if (iter->second.expires_at_monotonic_ms <=
                infra::Time::MonotonicMillis()) {
                expired = iter->second;
                sessions_.erase(iter);
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
                        AuthAuditResult::kRejected, "token expired");
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
                    target.empty() ? AuthPermissionToString(permission) : target,
                    AuthAuditResult::kRejected, "permission denied");
        return infra::Status::kNoPermission;
    }

 private:
    bool IsStarted() {
        infra::MutexGuard guard(&mutex_);
        return initialized_ && started_;
    }

    infra::Status CreateSession(const AuthUserRecord& user, LoginResult* result) {
        if (result == nullptr) {
            return infra::Status::kInvalidParam;
        }

        infra::MutexGuard guard(&mutex_);
        PruneExpiredSessionsLocked();
        if (sessions_.size() >= options_.max_sessions) {
            return infra::Status::kBusy;
        }

        ++sequence_;
        const int64_t now = infra::Time::MonotonicMillis();
        const int64_t wall_now = infra::Time::SystemTimeMillis();
        const int64_t ttl_ms =
            static_cast<int64_t>(options_.token_ttl_seconds) * 1000;
        const std::size_t user_hash =
            std::hash<std::string>()(user.user_name);
        const std::string token = auth_internal::MakeHexToken(
            static_cast<uint64_t>(now),
            static_cast<uint64_t>(wall_now),
            sequence_ ^ static_cast<uint64_t>(user_hash),
            reinterpret_cast<uintptr_t>(this));

        auth_internal::SessionRecord session;
        session.principal.user_name = user.user_name;
        session.principal.session_id = auth_internal::MakeSessionId(sequence_);
        session.principal.role = user.role;
        session.token = token;
        session.expires_at_monotonic_ms = now + ttl_ms;
        session.expires_at_ms = wall_now + ttl_ms;

        sessions_[token] = session;
        result->principal = session.principal;
        result->token = token;
        result->expires_at_ms = session.expires_at_ms;
        return infra::Status::kOk;
    }

    void PruneExpiredSessionsLocked() {
        const int64_t now = infra::Time::MonotonicMillis();
        for (auto iter = sessions_.begin(); iter != sessions_.end();) {
            if (iter->second.expires_at_monotonic_ms <= now) {
                iter = sessions_.erase(iter);
            } else {
                ++iter;
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
    std::unique_ptr<IAuthUserStore> user_store_;
    std::unique_ptr<IPasswordVerifier> password_verifier_;
    infra::Mutex mutex_;
    std::map<std::string, auth_internal::SessionRecord> sessions_;
    IAuthAuditSink* audit_sink_ = nullptr;
    uint64_t sequence_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IAuthService> CreateAuthService(
    const AuthServiceOptions& options,
    std::unique_ptr<IAuthUserStore> user_store,
    std::unique_ptr<IPasswordVerifier> password_verifier) {
    return std::unique_ptr<IAuthService>(
        new AuthServiceImpl(options, std::move(user_store),
                            std::move(password_verifier)));
}

}  // namespace live_stream
