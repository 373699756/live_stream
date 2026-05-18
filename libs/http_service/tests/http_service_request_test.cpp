#include "http_service.h"

#include "auth_service.h"
#include "config_service.h"
#include "logger_service.h"
#include "media_service.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

class FakeAuthService : public live_stream::IAuthService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    bool SetAuditSink(live_stream::IAuthAuditSink* sink) override {
        (void)sink;
        return true;
    }

    live_stream::LoginResult Login(
        const live_stream::LoginRequest& request) override {
        live_stream::LoginResult result;
        if (request.user_name != "admin" || request.password != "pass") {
            return result;
        }
        result.principal.user_name = "admin";
        result.principal.session_id = "session-1";
        result.principal.role = live_stream::AuthRole::kAdmin;
        result.token = "admin-token";
        result.expires_at_ms = 1234;
        return result;
    }

    bool Logout(const live_stream::RequestContext& context) override {
        return !context.session_id.empty();
    }

    live_stream::TokenValidationResult ValidateToken(
        const std::string& token) override {
        live_stream::TokenValidationResult result;
        if (token == "admin-token") {
            result.principal.user_name = "admin";
            result.principal.session_id = "session-1";
            result.principal.role = live_stream::AuthRole::kAdmin;
            result.expires_at_ms = 1234;
            return result;
        }
        if (token == "viewer-token") {
            result.principal.user_name = "viewer";
            result.principal.session_id = "session-2";
            result.principal.role = live_stream::AuthRole::kViewer;
            return result;
        }
        return result;
    }

    bool CheckPermission(
        const live_stream::AuthPrincipal& principal,
        live_stream::AuthPermission permission,
        const std::string& target) override {
        (void)target;
        if (principal.role == live_stream::AuthRole::kAdmin) {
            return true;
        }
        if (permission == live_stream::AuthPermission::kReadStatus ||
            permission == live_stream::AuthPermission::kPreviewVideo) {
            return true;
        }
        return false;
    }
};

class FakeConfigService : public live_stream::IConfigService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    bool SetValue(const std::string& name,
                  const live_stream::ConfigJson& value) override {
        if (name.empty()) {
            return false;
        }
        value_ = value;
        return true;
    }

    live_stream::ConfigJson GetValue(const std::string& name) override {
        if (name.empty()) {
            return live_stream::ConfigJson();
        }
        return value_;
    }

    live_stream::ConfigJson GetDefault(const std::string&) override {
        return live_stream::ConfigJson();
    }
    bool RestoreDefaults() override { return true; }
    bool SaveFile() override { return true; }
    bool RegisterApply(const std::string&, live_stream::ConfigProc) override {
        return true;
    }
    bool RegisterVerify(const std::string&, live_stream::ConfigProc) override {
        return true;
    }

private:
    live_stream::ConfigJson value_ = {{"enabled", true}};
};

class FakeLoggerService : public live_stream::ILoggerService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    bool RecordOperation(
        const live_stream::OperationRecord& record) override {
        records_.push_back(record);
        return true;
    }

    std::vector<live_stream::OperationRecord> QueryOperations(
        const live_stream::OperationLogQuery& query) override {
        (void)query;
        return records_;
    }

    bool ExportOperations(
        const live_stream::OperationLogExportOptions& options) override {
        (void)options;
        return true;
    }

    std::vector<live_stream::OperationRecord> records_;
};

live_stream::HttpRequest Request(live_stream::HttpMethod method,
                                 const std::string& path,
                                 const std::string& body,
                                 const std::string& token) {
    live_stream::HttpRequest request;
    request.method = method;
    request.path = path;
    request.body = body;
    request.client_ip = "192.0.2.10";
    request.headers["User-Agent"] = "unit-test";
    if (!token.empty()) {
        request.headers["Authorization"] = "Bearer " + token;
    }
    return request;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    FakeAuthService auth;
    FakeConfigService config;
    FakeLoggerService logger;

    live_stream::HttpServiceDependencies deps;
    deps.auth_service = &auth;
    deps.config_service = &config;
    deps.logger_service = &logger;

    live_stream::HttpServiceOptions options;
    std::unique_ptr<live_stream::IHttpService> service =
        live_stream::CreateHttpService(options, deps);
    if (!service->Init()) {
        return 1;
    }

    live_stream::HttpResponse login =
        service->HandleRequest(Request(live_stream::HttpMethod::kPost,
                                       "/api/auth/login",
                                       "{\"user_name\":\"admin\",\"password\":\"pass\"}",
                                       ""));
    if (login.status_code != 200 || !Contains(login.body, "admin-token")) {
        return 2;
    }

    live_stream::HttpResponse no_token =
        service->HandleRequest(Request(live_stream::HttpMethod::kPut,
                                       "/api/config/video",
                                       "{\"bitrate\":1024}",
                                       ""));
    if (no_token.status_code != 401) {
        return 3;
    }

    live_stream::HttpResponse denied =
        service->HandleRequest(Request(live_stream::HttpMethod::kPut,
                                       "/api/config/video",
                                       "{\"bitrate\":1024}",
                                       "viewer-token"));
    if (denied.status_code != 403 || logger.records_.empty()) {
        return 4;
    }

    live_stream::HttpResponse put_config =
        service->HandleRequest(Request(live_stream::HttpMethod::kPut,
                                       "/api/config/video",
                                       "{\"bitrate\":2048}",
                                       "admin-token"));
    if (put_config.status_code != 200) {
        return 5;
    }

    live_stream::HttpResponse get_config =
        service->HandleRequest(Request(live_stream::HttpMethod::kGet,
                                       "/api/config/video",
                                       "",
                                       "admin-token"));
    if (get_config.status_code != 200 ||
        !Contains(get_config.body, "2048")) {
        return 6;
    }

    live_stream::HttpResponse not_impl =
        service->HandleRequest(Request(live_stream::HttpMethod::kPost,
                                       "/api/upgrade",
                                       "",
                                       "admin-token"));
    if (not_impl.status_code != 501) {
        return 7;
    }

    live_stream::HttpServiceStats stats = service->GetStats();
    if (stats.total_requests != 6 || stats.permission_denied != 1) {
        return 8;
    }

    live_stream::MediaService media;
    live_stream::HttpServiceDependencies media_deps = deps;
    media_deps.media_service = &media;
    std::unique_ptr<live_stream::IHttpService> media_service =
        live_stream::CreateHttpService(options, media_deps);
    if (!media_service->Init()) {
        return 9;
    }
    live_stream::HttpResponse capabilities =
        media_service->HandleRequest(Request(live_stream::HttpMethod::kGet,
                                             "/api/media/capabilities",
                                             "",
                                             ""));
    if (capabilities.status_code != 200 ||
        !Contains(capabilities.body, "1920") ||
        !Contains(capabilities.body, "h265") ||
        !Contains(capabilities.body, "\"codec\":\"jpeg\"") ||
        !Contains(capabilities.body, "\"codec\":\"mjpeg\"") ||
        !Contains(capabilities.body, "\"image\"") ||
        !Contains(capabilities.body, "\"brightness\"")) {
        return 10;
    }

    const std::string invalid_video =
        "{\"streams\":{\"main\":{\"codec\":\"h264\",\"resolution\":\"9999x9999\","
        "\"fps\":25,\"bitrate_kbps\":4096,\"rate_control\":\"cbr\",\"gop\":50},"
        "\"sub\":{\"codec\":\"h264\",\"resolution\":\"640x360\",\"fps\":15,"
        "\"bitrate_kbps\":768,\"rate_control\":\"cbr\",\"gop\":30}}}";
    live_stream::HttpResponse invalid_config =
        media_service->HandleRequest(Request(live_stream::HttpMethod::kPut,
                                             "/api/config/video",
                                             invalid_video,
                                             "admin-token"));
    if (invalid_config.status_code != 400 ||
        !Contains(invalid_config.body, "unsupported resolution")) {
        return 11;
    }
    const std::string invalid_image =
        "{\"basic\":{\"brightness\":200},\"exposure\":{\"mode\":\"auto\"}}";
    live_stream::HttpResponse invalid_image_config =
        media_service->HandleRequest(Request(live_stream::HttpMethod::kPut,
                                             "/api/config/image",
                                             invalid_image,
                                             "admin-token"));
    if (invalid_image_config.status_code != 400 ||
        !Contains(invalid_image_config.body, "unsupported value")) {
        return 12;
    }

    service->Stop();
    service->Deinit();
    media_service->Stop();
    media_service->Deinit();
    return 0;
}
