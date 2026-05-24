/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: auth_service.h
 * Brief: 定义统一鉴权 service 的 public API。
 */

#ifndef LIVE_STREAM_AUTH_SERVICE_H_
#define LIVE_STREAM_AUTH_SERVICE_H_

#include "request_context.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IConfigService;

constexpr std::size_t kMaxAuthUserNameLength = 64;
constexpr std::size_t kMaxAuthPasswordLength = 256;

/**
 * @brief IPC 管理用户角色。
 */
enum class AuthRole {
    kAdmin,
    kOperator,
    kViewer,
};

/**
 * @brief 统一鉴权权限点。
 */
enum class AuthPermission {
    kReadStatus,
    kPreviewVideo,
    kModifyConfig,
    kUpgrade,
    kReboot,
    kFactoryReset,
    kManageUsers,
};

/**
 * @brief 当前认证主体。
 *
 * 不包含 token、密码、认证头等敏感明文，可安全传递给其他 service 做权限判断。
 */
struct AuthPrincipal {
    std::string user_name;
    std::string session_id;
    AuthRole role = AuthRole::kViewer;
    bool must_change_password = false;
};

/**
 * @brief 用户存储记录。
 *
 * password_credential 是密码校验器可理解的不透明凭据。当前生产格式固定为
 * pbkdf2-sha256:<iterations>:<salt_hex>:<hash_hex>；auth_service 不会把它写入日志或审计。
 */
struct AuthUserRecord {
    std::string user_name;
    std::string password_credential;
    AuthRole role = AuthRole::kViewer;
    bool enabled = true;
    bool must_change_password = false;
};

/**
 * @brief 登录请求。
 */
struct LoginRequest {
    live_stream::RequestContext context;
    std::string user_name;
    std::string password;
};

/**
 * @brief 登录成功结果。
 */
struct LoginResult {
    AuthPrincipal principal;
    std::string token;
    int64_t expires_at_ms = 0;
    bool must_change_password = false;
};

/**
 * @brief token 校验结果。
 */
struct TokenValidationResult {
    AuthPrincipal principal;
    int64_t expires_at_ms = 0;
    bool must_change_password = false;
};

/**
 * @brief 修改当前用户密码请求。
 */
struct ChangePasswordRequest {
    live_stream::RequestContext context;
    std::string old_password;
    std::string new_password;
};

/**
 * @brief 鉴权审计动作。
 */
enum class AuthAuditAction {
    kLogin,
    kLogout,
    kAuthFailed,
    kTokenExpired,
    kPermissionDenied,
};

/**
 * @brief 鉴权审计结果。
 */
enum class AuthAuditResult {
    kSuccess,
    kFailed,
    kRejected,
};

/**
 * @brief 鉴权审计记录。
 *
 * 该结构不包含密码、token、认证头或密码凭据，调用方可将其适配为 logger_service
 * 的 OperationRecord。
 */
struct AuthAuditRecord {
    live_stream::RequestContext context;
    std::string user_name;
    std::string session_id;
    std::string module;
    AuthAuditAction action = AuthAuditAction::kLogin;
    std::string target;
    AuthAuditResult result = AuthAuditResult::kSuccess;
    std::string reason;
};

/**
 * @brief auth_service 创建参数。
 */
struct AuthServiceOptions {
    uint32_t token_ttl_seconds = 30 * 60;
    uint32_t max_sessions = 16;
    uint32_t max_sessions_per_user = 4;
    uint32_t lockout_failures = 5;
    uint32_t lockout_seconds = 300;
    uint32_t password_min_length = 1;
    bool password_require_number = false;
    bool password_require_symbol = false;
};

struct AuthServiceDependencies {
    IConfigService* config_service = nullptr;
};

struct AuthStats {
    uint64_t login_success = 0;
    uint64_t login_failed = 0;
    uint64_t token_validation_failed = 0;
    uint64_t expired_sessions = 0;
    uint64_t lockout_count = 0;
    uint64_t config_apply_count = 0;
    uint64_t config_apply_failed_count = 0;
    uint32_t active_sessions = 0;
};

class IAuthTokenGenerator {
public:
    virtual ~IAuthTokenGenerator() = default;

    virtual std::string GenerateToken() = 0;
};

/**
 * @brief 用户存储接口。
 */
class IAuthUserStore {
public:
    virtual ~IAuthUserStore() = default;

    virtual AuthUserRecord FindUser(
        const std::string& user_name) = 0;
    virtual bool UpdatePassword(
        const std::string& user_name,
        const std::string& password_credential,
        bool must_change_password) = 0;
    virtual bool Reload() { return true; }
};

/**
 * @brief 密码校验接口。
 */
class IPasswordVerifier {
public:
    virtual ~IPasswordVerifier() = default;

    virtual bool VerifyPassword(
        const std::string& password,
        const std::string& password_credential) = 0;
};

/**
 * @brief 鉴权审计 sink。
 */
class IAuthAuditSink {
public:
    virtual ~IAuthAuditSink() = default;

    virtual bool RecordAuthOperation(
        const AuthAuditRecord& record) = 0;
};

/**
 * @brief 统一鉴权 public interface。
 */
class IAuthService {
public:
    virtual ~IAuthService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual bool SetAuditSink(IAuthAuditSink* sink) = 0;
    virtual LoginResult Login(const LoginRequest& request) = 0;
    virtual bool Logout(const live_stream::RequestContext& context) = 0;
    virtual TokenValidationResult ValidateToken(
        const std::string& token) = 0;
    virtual bool ChangePassword(
        const ChangePasswordRequest& request) = 0;
    virtual bool CheckPermission(
        const AuthPrincipal& principal,
        AuthPermission permission,
        const std::string& target) = 0;
    virtual AuthStats GetStats() const { return AuthStats{}; }
};

/**
 * @brief 创建仅用于开发和测试的内存用户存储。
 */
std::unique_ptr<IAuthUserStore> CreateMemoryAuthUserStore(
    const std::vector<AuthUserRecord>& users);

/**
 * @brief 创建明文密码校验器。
 *
 * 该校验器只适用于单元测试、开发调试或无生产凭据接入的骨架阶段。
 */
std::unique_ptr<IPasswordVerifier> CreatePlainTextPasswordVerifier();

/**
 * @brief 创建默认密码校验器。
 *
 * 支持的生产凭据格式为 pbkdf2-sha256:<iterations>:<salt_hex>:<hash_hex>。
 */
std::unique_ptr<IPasswordVerifier> CreatePbkdf2PasswordVerifier();

/**
 * @brief 创建统一鉴权 service。
 */
std::unique_ptr<IAuthService> CreateAuthService(
    const AuthServiceOptions& options,
    std::unique_ptr<IAuthUserStore> user_store,
    std::unique_ptr<IPasswordVerifier> password_verifier);

std::unique_ptr<IAuthService> CreateAuthService(
    const AuthServiceOptions& options,
    const AuthServiceDependencies& dependencies,
    std::unique_ptr<IAuthUserStore> user_store,
    std::unique_ptr<IPasswordVerifier> password_verifier,
    IAuthTokenGenerator* token_generator = nullptr);

const char* AuthRoleToString(AuthRole role);
const char* AuthPermissionToString(AuthPermission permission);
const char* AuthAuditActionToString(AuthAuditAction action);
const char* AuthAuditResultToString(AuthAuditResult result);

}  // namespace live_stream

#endif  // LIVE_STREAM_AUTH_SERVICE_H_
