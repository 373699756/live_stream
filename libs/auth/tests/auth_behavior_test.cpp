#include "auth.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class CountingTokenGenerator : public live_stream::IAuthTokenGenerator {
public:
    std::string GenerateToken() override {
        ++next_;
        return std::string("token-") + std::to_string(next_);
    }

private:
    uint64_t next_ = 0;
};

class TestAuditSink : public live_stream::IAuthAuditSink {
public:
    bool RecordAuthOperation(
        const live_stream::AuthAuditRecord& record) override {
        ++count;
        last = record;
        if (record.reason.find("secret") != std::string::npos ||
            record.target.find("secret") != std::string::npos) {
            leaked_sensitive = true;
        }
        return true;
    }

    int count = 0;
    bool leaked_sensitive = false;
    live_stream::AuthAuditRecord last;
};

std::unique_ptr<live_stream::IAuth> CreateStarted(
    TestAuditSink* audit_sink,
    CountingTokenGenerator* token_generator,
    uint32_t token_ttl_seconds = 30) {
    std::vector<live_stream::AuthUserRecord> users;
    users.push_back(live_stream::AuthUserRecord{
        "admin", "admin-pass", live_stream::AuthRole::kAdmin, true, false});
    users.push_back(live_stream::AuthUserRecord{
        "viewer", "viewer-pass", live_stream::AuthRole::kViewer, true, false});
    users.push_back(live_stream::AuthUserRecord{
        "disabled", "disabled-pass", live_stream::AuthRole::kViewer, false,
        false});

    live_stream::AuthOptions options;
    options.token_ttl_seconds = token_ttl_seconds;
    options.max_sessions = 4;
    options.max_sessions_per_user = 1;
    options.lockout_failures = 2;
    options.lockout_seconds = 1;
    options.password_min_length = 8;

    live_stream::AuthUsersOptions auth_users_options;
    auth_users_options.kind = live_stream::AuthUsersKind::kMemory;
    auth_users_options.users = users;

    std::unique_ptr<live_stream::IAuth> service =
        live_stream::CreateAuth(
            options,
            live_stream::CreateAuthUsers(auth_users_options),
            live_stream::CreatePasswordVerifier(
                live_stream::PasswordVerifierKind::kPlainText),
            token_generator);
    if (!service || !service->Start() || !service->SetAuditSink(audit_sink)) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    CountingTokenGenerator token_generator;
    TestAuditSink audit_sink;
    std::unique_ptr<live_stream::IAuth> service =
        CreateStarted(&audit_sink, &token_generator);
    if (!service || !service->IsStarted()) {
        return 1;
    }

    live_stream::LoginRequest request;
    request.context.request_id = "req-1";
    request.context.client_ip = "127.0.0.1";
    request.user_name = "admin";
    request.password = "admin-pass";
    live_stream::LoginResult login = service->Login(request);
    if (login.token != "token-1" ||
        login.principal.user_name != "admin" ||
        login.principal.role != live_stream::AuthRole::kAdmin ||
        login.principal.session_id.empty()) {
        return 2;
    }
    if (audit_sink.last.action != live_stream::AuthAuditAction::kLogin ||
        audit_sink.last.result != live_stream::AuthAuditResult::kSuccess ||
        audit_sink.leaked_sensitive) {
        return 3;
    }

    live_stream::TokenValidationResult validation =
        service->ValidateToken(login.token);
    if (validation.principal.user_name != "admin" ||
        validation.principal.session_id != login.principal.session_id) {
        return 4;
    }
    if (!service->CheckPermission(validation.principal,
                                  live_stream::AuthPermission::kUpgrade,
                                  "upgrade")) {
        return 5;
    }

    request.user_name = "viewer";
    request.password = "viewer-pass";
    live_stream::LoginResult viewer_login = service->Login(request);
    if (viewer_login.token != "token-2") {
        return 6;
    }
    if (service->CheckPermission(viewer_login.principal,
                                 live_stream::AuthPermission::kUpgrade,
                                 "upgrade")) {
        return 7;
    }
    if (audit_sink.last.action !=
        live_stream::AuthAuditAction::kPermissionDenied) {
        return 8;
    }

    request.password = "bad";
    live_stream::LoginResult failed_login = service->Login(request);
    if (!failed_login.token.empty() ||
        audit_sink.last.action != live_stream::AuthAuditAction::kAuthFailed ||
        audit_sink.last.reason != "bad_password") {
        return 9;
    }

    live_stream::RequestContext logout_context;
    logout_context.user_name = login.principal.user_name;
    logout_context.session_id = login.principal.session_id;
    if (!service->Logout(logout_context)) {
        return 10;
    }
    if (!service->ValidateToken(login.token).principal.user_name.empty()) {
        return 11;
    }

    live_stream::AuthStats stats = service->GetStats();
    if (stats.login_success != 2 || stats.login_failed == 0 ||
        stats.active_sessions != 1) {
        return 12;
    }

    service->Stop();
    if (service->IsStarted() || !service->Login(request).token.empty()) {
        return 13;
    }
    return 0;
}
