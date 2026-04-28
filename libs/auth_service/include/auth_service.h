/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: auth_service.h
 * Brief: 定义统一鉴权 service 的 public API。
 */

#ifndef LIVE_STREAM_AUTH_SERVICE_H_
#define LIVE_STREAM_AUTH_SERVICE_H_

#include "infra/status.h"
#include "infra/request_context.h"
#include "infra/service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

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
};

/**
 * @brief 用户存储记录。
 *
 * password_credential 是密码校验器可理解的不透明凭据。auth_service 不解释该字段，
 * 也不会把它写入日志或审计。
 */
struct AuthUserRecord {
    std::string user_name;
    std::string password_credential;
    AuthRole role = AuthRole::kViewer;
    bool enabled = true;
};

/**
 * @brief 登录请求。
 */
struct LoginRequest {
    infra::RequestContext context;
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
};

/**
 * @brief token 校验结果。
 */
struct TokenValidationResult {
    AuthPrincipal principal;
    int64_t expires_at_ms = 0;
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
    infra::RequestContext context;
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
};

/**
 * @brief 用户存储接口。
 */
class IAuthUserStore {
 public:
    virtual ~IAuthUserStore() = default;

    virtual infra::Result<AuthUserRecord> FindUser(
        const std::string& user_name) = 0;
};

/**
 * @brief 密码校验接口。
 */
class IPasswordVerifier {
 public:
    virtual ~IPasswordVerifier() = default;

    virtual infra::Status VerifyPassword(
        const std::string& password,
        const std::string& password_credential) = 0;
};

/**
 * @brief 鉴权审计 sink。
 */
class IAuthAuditSink {
 public:
    virtual ~IAuthAuditSink() = default;

    virtual infra::Status RecordAuthOperation(
        const AuthAuditRecord& record) = 0;
};

/**
 * @brief 统一鉴权 public interface。
 */
class IAuthService : public infra::IService {
 public:
    virtual infra::Status SetAuditSink(IAuthAuditSink* sink) = 0;
    virtual infra::Result<LoginResult> Login(const LoginRequest& request) = 0;
    virtual infra::Status Logout(const infra::RequestContext& context) = 0;
    virtual infra::Result<TokenValidationResult> ValidateToken(
        const std::string& token) = 0;
    virtual infra::Status CheckPermission(
        const AuthPrincipal& principal,
        AuthPermission permission,
        const std::string& target) = 0;
};

/**
 * @brief 创建仅用于开发和测试的内存用户存储。
 */
std::unique_ptr<IAuthUserStore> CreateMemoryAuthUserStore(
    const std::vector<AuthUserRecord>& users);

/**
 * @brief 创建 JSON 文件用户存储。
 *
 * 文件只应保存 password_credential，不允许保存 password 明文字段。
 */
std::unique_ptr<IAuthUserStore> CreateJsonAuthUserStore(
    const std::string& config_path);

/**
 * @brief 创建明文密码校验器。
 *
 * 该校验器只适用于单元测试、开发调试或无生产凭据接入的骨架阶段。
 */
std::unique_ptr<IPasswordVerifier> CreatePlainTextPasswordVerifier();

/**
 * @brief 创建 SHA-256 盐值密码校验器。
 *
 * 支持的凭据格式为 sha256:<salt_hex>:<hash_hex>，其中 hash 为 SHA256(salt + password)。
 */
std::unique_ptr<IPasswordVerifier> CreateSha256PasswordVerifier();

/**
 * @brief 生成 SHA-256 盐值密码凭据。
 *
 * @param password 明文密码，只用于生成凭据，不应写入配置文件。
 * @param salt_hex 十六进制盐值字符串。
 *
 * @return 成功返回 sha256:<salt_hex>:<hash_hex>；参数非法返回 kInvalidParam。
 */
infra::Result<std::string> MakeSha256PasswordCredential(
    const std::string& password,
    const std::string& salt_hex);

/**
 * @brief 创建统一鉴权 service。
 */
std::unique_ptr<IAuthService> CreateAuthService(
    const AuthServiceOptions& options,
    std::unique_ptr<IAuthUserStore> user_store,
    std::unique_ptr<IPasswordVerifier> password_verifier);

const char* AuthRoleToString(AuthRole role);
const char* AuthPermissionToString(AuthPermission permission);
const char* AuthAuditActionToString(AuthAuditAction action);
const char* AuthAuditResultToString(AuthAuditResult result);

}  // namespace live_stream

#endif  // LIVE_STREAM_AUTH_SERVICE_H_
