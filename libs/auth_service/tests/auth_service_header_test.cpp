#include "auth_service.h"

#include "infra/time.h"
#include "infra/fs.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

class TestAuditSink : public live_stream::IAuthAuditSink {
 public:
    infra::Status RecordAuthOperation(
        const live_stream::AuthAuditRecord& record) override {
        ++count;
        last = record;
        if (record.reason.find("secret") != std::string::npos ||
            record.target.find("secret") != std::string::npos) {
            leaked_sensitive = true;
        }
        return infra::Status::kOk;
    }

    int count = 0;
    bool leaked_sensitive = false;
    live_stream::AuthAuditRecord last;
};

int Check(bool condition, int code) {
    return condition ? 0 : code;
}

std::unique_ptr<live_stream::IAuthService> CreateStartedService(
    TestAuditSink* audit_sink,
    uint32_t token_ttl_seconds) {
    std::vector<live_stream::AuthUserRecord> users;
    live_stream::AuthUserRecord admin;
    admin.user_name = "admin";
    admin.password_credential = "admin-pass";
    admin.role = live_stream::AuthRole::kAdmin;
    users.push_back(admin);

    live_stream::AuthUserRecord viewer;
    viewer.user_name = "viewer";
    viewer.password_credential = "viewer-pass";
    viewer.role = live_stream::AuthRole::kViewer;
    users.push_back(viewer);

    live_stream::AuthServiceOptions options;
    options.token_ttl_seconds = token_ttl_seconds;
    options.max_sessions = 4;

    std::unique_ptr<live_stream::IAuthService> service =
        live_stream::CreateAuthService(
            options, live_stream::CreateMemoryAuthUserStore(users),
            live_stream::CreatePlainTextPasswordVerifier());
    if (service == nullptr || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk ||
        service->SetAuditSink(audit_sink) != infra::Status::kOk) {
        return std::unique_ptr<live_stream::IAuthService>();
    }
    return service;
}

}  // namespace

int main() {
    TestAuditSink audit_sink;
    std::unique_ptr<live_stream::IAuthService> service =
        CreateStartedService(&audit_sink, 30);
    if (Check(service != nullptr, 1) != 0) {
        return 1;
    }
    if (std::string(service->Name()) != "auth_service") {
        return 2;
    }

    live_stream::LoginRequest request;
    request.context.request_id = "req-1";
    request.context.client_ip = "127.0.0.1";
    request.user_name = "admin";
    request.password = "admin-pass";

    infra::Result<live_stream::LoginResult> login = service->Login(request);
    if (!login.IsOk() || login.value.token.empty() ||
        login.value.principal.role != live_stream::AuthRole::kAdmin) {
        return 3;
    }
    if (audit_sink.count != 1 ||
        audit_sink.last.action != live_stream::AuthAuditAction::kLogin ||
        audit_sink.last.result != live_stream::AuthAuditResult::kSuccess) {
        return 4;
    }

    infra::Result<live_stream::TokenValidationResult> validation =
        service->ValidateToken(login.value.token);
    if (!validation.IsOk() ||
        validation.value.principal.user_name != "admin") {
        return 5;
    }
    if (service->CheckPermission(validation.value.principal,
                                 live_stream::AuthPermission::kUpgrade,
                                 "upgrade") != infra::Status::kOk) {
        return 6;
    }

    request.user_name = "viewer";
    request.password = "viewer-pass";
    infra::Result<live_stream::LoginResult> viewer_login =
        service->Login(request);
    if (!viewer_login.IsOk()) {
        return 7;
    }
    if (service->CheckPermission(viewer_login.value.principal,
                                 live_stream::AuthPermission::kUpgrade,
                                 "upgrade") != infra::Status::kNoPermission) {
        return 8;
    }
    if (audit_sink.last.action !=
        live_stream::AuthAuditAction::kPermissionDenied) {
        return 9;
    }

    request.password = "bad-secret";
    infra::Result<live_stream::LoginResult> failed_login =
        service->Login(request);
    if (failed_login.status != infra::Status::kUnauthorized) {
        return 10;
    }
    if (audit_sink.last.action != live_stream::AuthAuditAction::kAuthFailed ||
        audit_sink.leaked_sensitive) {
        return 11;
    }

    infra::RequestContext logout_context;
    logout_context.user_name = "admin";
    logout_context.session_id = login.value.principal.session_id;
    if (service->Logout(logout_context) != infra::Status::kOk) {
        return 12;
    }
    if (service->ValidateToken(login.value.token).status !=
        infra::Status::kUnauthorized) {
        return 13;
    }

    service->Stop();
    service->Deinit();

    TestAuditSink expire_audit;
    std::unique_ptr<live_stream::IAuthService> expiring =
        CreateStartedService(&expire_audit, 1);
    if (expiring == nullptr) {
        return 14;
    }
    request.user_name = "viewer";
    request.password = "viewer-pass";
    viewer_login = expiring->Login(request);
    if (!viewer_login.IsOk()) {
        return 15;
    }
    infra::Time::SleepMillis(1100);
    if (expiring->ValidateToken(viewer_login.value.token).status !=
        infra::Status::kUnauthorized) {
        return 16;
    }
    if (expire_audit.last.action !=
        live_stream::AuthAuditAction::kTokenExpired) {
        return 17;
    }

    expiring->Stop();
    expiring->Deinit();

    const std::string path = "/tmp/live_stream_auth_users_test.json";
    infra::Result<std::string> credential =
        live_stream::MakeSha256PasswordCredential(
            "json-pass", "00112233445566778899aabbccddeeff");
    if (!credential.IsOk()) {
        return 18;
    }
    const std::string json =
        "{\n"
        "  \"version\": 1,\n"
        "  \"users\": [\n"
        "    {\n"
        "      \"user_name\": \"json-admin\",\n"
        "      \"role\": \"admin\",\n"
        "      \"enabled\": true,\n"
        "      \"password_credential\": \"" + credential.value + "\"\n"
        "    }\n"
        "  ]\n"
        "}\n";
    if (json.find("json-pass") != std::string::npos ||
        infra::File::WriteAll(path, json) != infra::Status::kOk) {
        return 19;
    }

    live_stream::AuthServiceOptions json_options;
    json_options.token_ttl_seconds = 30;
    json_options.max_sessions = 2;
    std::unique_ptr<live_stream::IAuthService> json_service =
        live_stream::CreateAuthService(
            json_options, live_stream::CreateJsonAuthUserStore(path),
            live_stream::CreateSha256PasswordVerifier());
    if (json_service == nullptr ||
        json_service->Init() != infra::Status::kOk ||
        json_service->Start() != infra::Status::kOk) {
        std::remove(path.c_str());
        return 20;
    }
    live_stream::LoginRequest json_login_request;
    json_login_request.user_name = "json-admin";
    json_login_request.password = "json-pass";
    infra::Result<live_stream::LoginResult> json_login =
        json_service->Login(json_login_request);
    if (!json_login.IsOk() ||
        json_login.value.principal.role != live_stream::AuthRole::kAdmin) {
        std::remove(path.c_str());
        return 21;
    }
    json_login_request.password = "bad-json-pass";
    if (json_service->Login(json_login_request).status !=
        infra::Status::kUnauthorized) {
        std::remove(path.c_str());
        return 22;
    }
    json_service->Stop();
    json_service->Deinit();
    std::remove(path.c_str());
    return 0;
}
