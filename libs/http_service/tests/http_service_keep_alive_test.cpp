#include "http_service.h"

#include "auth_service.h"
#include "config_service.h"
#include "net_service.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>

namespace {

class FakeAuthService : public live_stream::IAuthService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_auth"; }

    infra::Status SetAuditSink(live_stream::IAuthAuditSink*) override {
        return infra::Status::kOk;
    }

    infra::Result<live_stream::LoginResult> Login(
        const live_stream::LoginRequest& request) override {
        if (request.user_name != "admin" || request.password != "pass") {
            return infra::Result<live_stream::LoginResult>::Fail(
                infra::Status::kUnauthorized);
        }
        live_stream::LoginResult result;
        result.principal.user_name = "admin";
        result.principal.session_id = "session-1";
        result.principal.role = live_stream::AuthRole::kAdmin;
        result.token = "admin-token";
        return infra::Result<live_stream::LoginResult>::Ok(result);
    }

    infra::Status Logout(const live_stream::RequestContext&) override {
        return infra::Status::kOk;
    }

    infra::Result<live_stream::TokenValidationResult> ValidateToken(
        const std::string&) override {
        return infra::Result<live_stream::TokenValidationResult>::Fail(
            infra::Status::kUnauthorized);
    }

    infra::Status CheckPermission(const live_stream::AuthPrincipal&,
                                  live_stream::AuthPermission,
                                  const std::string&) override {
        return infra::Status::kNoPermission;
    }
};

class FakeConfigService : public live_stream::IConfigService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_config"; }

    infra::Status SetValue(const std::string&, const live_stream::ConfigJson&) override {
        return infra::Status::kOk;
    }

    infra::Status GetValue(const std::string&, live_stream::ConfigJson* value) override {
        if (value != nullptr) {
            *value = live_stream::ConfigJson::object();
        }
        return infra::Status::kOk;
    }

    infra::Status GetDefault(const std::string&, live_stream::ConfigJson*) override {
        return infra::Status::kOk;
    }
    infra::Status RestoreDefaults() override { return infra::Status::kOk; }
    infra::Status SaveFile() override { return infra::Status::kOk; }
    infra::Status RegisterApply(const std::string&, live_stream::ConfigProc) override {
        return infra::Status::kOk;
    }
    infra::Status RegisterVerify(const std::string&, live_stream::ConfigProc) override {
        return infra::Status::kOk;
    }
};

int ConnectTo(const live_stream::HttpListenAddress& address) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout {};
    timeout.tv_sec = 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    if (inet_pton(AF_INET, address.ip.c_str(), &addr.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool SendAll(int fd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = send(fd, data.data() + offset,
                               data.size() - offset, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        offset += static_cast<size_t>(n);
    }
    return true;
}

std::string ReadUntilClose(int fd) {
    std::string out;
    char buffer[512];
    while (true) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            out.append(buffer, static_cast<size_t>(n));
            continue;
        }
        break;
    }
    return out;
}

size_t Count(const std::string& value, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

}  // namespace

int main() {
    auto netframe = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!netframe.IsOk()) {
        return 1;
    }

    FakeAuthService auth;
    FakeConfigService config;
    live_stream::HttpServiceDependencies deps;
    deps.net_engine = netframe.value.get();
    deps.auth_service = &auth;
    deps.config_service = &config;

    live_stream::HttpServiceOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;
    options.enable_keep_alive = true;
    options.executor_worker_count = 2;
    options.max_requests_per_connection = 2;
    options.max_pipelined_requests = 2;

    auto http = live_stream::CreateHttpService(options, deps);
    if (!http || http->Init() != infra::Status::kOk ||
        http->Start() != infra::Status::kOk) {
        return 2;
    }
    auto address = http->LocalAddress();
    if (!address.IsOk()) {
        return 3;
    }
    const int fd = ConnectTo(address.value);
    if (fd < 0) {
        return 4;
    }

    const std::string body = "{\"user_name\":\"admin\",\"password\":\"pass\"}";
    const std::string request =
        "POST /api/auth/login HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    if (!SendAll(fd, request + request)) {
        close(fd);
        return 5;
    }

    const std::string response = ReadUntilClose(fd);
    close(fd);
    http->Stop();
    http->Deinit();
    netframe.value->Stop();

    if (Count(response, "HTTP/1.1 200 OK") != 2 ||
        Count(response, "admin-token") != 2 ||
        response.find("Connection: keep-alive") == std::string::npos ||
        response.find("Connection: close") == std::string::npos) {
        return 6;
    }
    return 0;
}
