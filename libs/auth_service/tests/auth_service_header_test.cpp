#include "auth_service.h"

#include "config_service.h"
#include "infra/time.h"
#include "infra/fs.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeConfigService : public live_stream::IConfigService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_config"; }

    infra::Status SetValue(const std::string& name,
                           const live_stream::ConfigJson& value) override {
        if (name != "user") {
            return infra::Status::kInvalidParam;
        }
        if (verify && verify(value) != infra::Status::kOk) {
            return infra::Status::kInvalidParam;
        }
        return apply ? apply(value) : infra::Status::kOk;
    }
    infra::Status GetValue(const std::string& name,
                           live_stream::ConfigJson* value) override {
        if (name != "user" || value == nullptr) {
            return infra::Status::kInvalidParam;
        }
        *value = user_config;
        return infra::Status::kOk;
    }
    infra::Status GetDefault(const std::string&,
                             live_stream::ConfigJson*) override {
        return infra::Status::kNotFound;
    }
    infra::Status RestoreDefaults() override { return infra::Status::kOk; }
    infra::Status SaveFile() override { return infra::Status::kOk; }
    infra::Status RegisterApply(const std::string& name,
                                live_stream::ConfigProc proc) override {
        if (name != "user") {
            return infra::Status::kInvalidParam;
        }
        apply = proc;
        return infra::Status::kOk;
    }
    infra::Status RegisterVerify(const std::string& name,
                                 live_stream::ConfigProc proc) override {
        if (name != "user") {
            return infra::Status::kInvalidParam;
        }
        verify = proc;
        return infra::Status::kOk;
    }

    live_stream::ConfigJson user_config = {
        {"accounts", live_stream::ConfigJson::array()},
        {"password_policy",
         {{"min_length", 8},
          {"require_number", true},
          {"require_symbol", false},
          {"lockout_failures", 2},
          {"lockout_seconds", 1}}},
        {"session",
         {{"token_ttl_seconds", 30}, {"max_sessions_per_user", 1}}}};
    live_stream::ConfigProc verify;
    live_stream::ConfigProc apply;
};

class CountingTokenGenerator : public live_stream::IAuthTokenGenerator {
 public:
    infra::Result<std::string> GenerateToken() override {
        ++next;
        char buffer[80] = {0};
        std::snprintf(buffer, sizeof(buffer),
                      "%064llu",
                      static_cast<unsigned long long>(next));
        return infra::Result<std::string>::Ok(buffer);
    }

    uint64_t next = 0;
};

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
    uint32_t token_ttl_seconds,
    live_stream::AuthServiceOptions* used_options = nullptr,
    live_stream::AuthServiceDependencies dependencies =
        live_stream::AuthServiceDependencies(),
    live_stream::IAuthTokenGenerator* token_generator = nullptr) {
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
    options.max_sessions_per_user = 4;
    options.lockout_failures = 5;
    options.lockout_seconds = 1;
    if (used_options != nullptr) {
        options = *used_options;
    }

    std::unique_ptr<live_stream::IAuthService> service =
        live_stream::CreateAuthService(
            options, dependencies, live_stream::CreateMemoryAuthUserStore(users),
            live_stream::CreatePlainTextPasswordVerifier(), token_generator);
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
    if (login.value.token.size() < 64) {
        return 23;
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

    TestAuditSink config_audit;
    FakeConfigService config_service;
    CountingTokenGenerator token_generator;
    live_stream::AuthServiceOptions configured_options;
    configured_options.token_ttl_seconds = 30;
    configured_options.max_sessions = 4;
    configured_options.max_sessions_per_user = 2;
    configured_options.lockout_failures = 2;
    configured_options.lockout_seconds = 1;
    live_stream::AuthServiceDependencies dependencies;
    dependencies.config_service = &config_service;
    std::unique_ptr<live_stream::IAuthService> configured =
        CreateStartedService(&config_audit, 30, &configured_options,
                             dependencies, &token_generator);
    if (configured == nullptr) {
        return 24;
    }
    live_stream::AuthStats auth_stats = configured->GetStats();
    if (auth_stats.config_apply_count != 1) {
        return 25;
    }

    live_stream::LoginRequest configured_request;
    configured_request.user_name = "viewer";
    configured_request.password = "viewer-pass";
    infra::Result<live_stream::LoginResult> first =
        configured->Login(configured_request);
    infra::Result<live_stream::LoginResult> second =
        configured->Login(configured_request);
    if (!first.IsOk() || !second.IsOk() ||
        first.value.token == second.value.token) {
        return 26;
    }
    if (configured->ValidateToken(first.value.token).status !=
        infra::Status::kUnauthorized) {
        return 27;
    }
    if (!configured->ValidateToken(second.value.token).IsOk()) {
        return 28;
    }

    configured_request.password = "wrong-pass";
    if (configured->Login(configured_request).status !=
        infra::Status::kUnauthorized ||
        configured->Login(configured_request).status !=
            infra::Status::kUnauthorized) {
        return 29;
    }
    configured_request.password = "viewer-pass";
    if (configured->Login(configured_request).status != infra::Status::kBusy) {
        return 30;
    }
    infra::Time::SleepMillis(1100);
    if (!configured->Login(configured_request).IsOk()) {
        return 31;
    }
    auth_stats = configured->GetStats();
    if (auth_stats.lockout_count == 0 || auth_stats.login_failed < 2) {
        return 32;
    }
    config_service.user_config["session"]["token_ttl_seconds"] = 1;
    if (config_service.SetValue("user", config_service.user_config) !=
        infra::Status::kOk) {
        return 33;
    }
    if (configured->GetStats().config_apply_count < 2) {
        return 34;
    }
    config_service.user_config["session"]["token_ttl_seconds"] = 0;
    if (config_service.SetValue("user", config_service.user_config) !=
        infra::Status::kInvalidParam) {
        return 35;
    }
    config_service.user_config["session"]["token_ttl_seconds"] = "bad";
    if (config_service.SetValue("user", config_service.user_config) !=
        infra::Status::kInvalidParam) {
        return 36;
    }
    return 0;
}
