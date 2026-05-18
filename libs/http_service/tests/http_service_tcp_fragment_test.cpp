#include "http_service.h"

#include "auth_service.h"
#include "config_service.h"
#include "logger_service.h"
#include "net_service.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeAuthService : public live_stream::IAuthService {
public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_auth"; }

    infra::Status SetAuditSink(live_stream::IAuthAuditSink* sink) override {
        (void)sink;
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
        result.expires_at_ms = 1234;
        return infra::Result<live_stream::LoginResult>::Ok(result);
    }

    infra::Status Logout(const live_stream::RequestContext& context) override {
        (void)context;
        return infra::Status::kOk;
    }

    infra::Result<live_stream::TokenValidationResult> ValidateToken(
        const std::string& token) override {
        (void)token;
        return infra::Result<live_stream::TokenValidationResult>::Fail(
            infra::Status::kUnauthorized);
    }

    infra::Status CheckPermission(
        const live_stream::AuthPrincipal& principal,
        live_stream::AuthPermission permission,
        const std::string& target) override {
        (void)principal;
        (void)permission;
        (void)target;
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

    infra::Status SetValue(const std::string& name,
                           const live_stream::ConfigJson& value) override {
        (void)name;
        (void)value;
        return infra::Status::kOk;
    }

    infra::Status GetValue(const std::string& name,
                           live_stream::ConfigJson* value) override {
        (void)name;
        if (value != nullptr) {
            *value = live_stream::ConfigJson::object();
        }
        return infra::Status::kOk;
    }

    infra::Status GetDefault(const std::string&, live_stream::ConfigJson*) override { return infra::Status::kOk; }
    infra::Status RestoreDefaults() override { return infra::Status::kOk; }
    infra::Status SaveFile() override { return infra::Status::kOk; }
    infra::Status RegisterApply(const std::string&, live_stream::ConfigProc) override { return infra::Status::kOk; }
    infra::Status RegisterVerify(const std::string&, live_stream::ConfigProc) override { return infra::Status::kOk; }
};

int ConnectTo(const live_stream::HttpListenAddress& address) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout{};
    timeout.tv_sec = 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    if (inet_pton(AF_INET, address.ip.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
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
    char buffer[256];
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

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
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
    auto http = live_stream::CreateHttpService(options, deps);
    if (!http || http->Init() != infra::Status::kOk ||
        http->Start() != infra::Status::kOk) {
        return 2;
    }

    infra::Result<live_stream::HttpListenAddress> address = http->LocalAddress();
    if (!address.IsOk()) {
        http->Stop();
        http->Deinit();
        return 3;
    }

    const int fd = ConnectTo(address.value);
    if (fd < 0) {
        http->Stop();
        http->Deinit();
        return 4;
    }

    const std::string body = "{\"user_name\":\"admin\",\"password\":\"pass\"}";
    const std::string part1 =
        "POST /api/auth/login HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: ";
    const std::string part2 =
        std::to_string(body.size()) + "\r\n\r\n" + body;
    if (!SendAll(fd, part1) || !SendAll(fd, part2)) {
        close(fd);
        http->Stop();
        http->Deinit();
        return 5;
    }

    const std::string response = ReadUntilClose(fd);
    close(fd);
    http->Stop();
    http->Deinit();
    netframe.value->Stop();

    if (!Contains(response, "HTTP/1.1 200 OK") ||
        !Contains(response, "admin-token") ||
        !Contains(response, "Connection: close")) {
        return 6;
    }
    return 0;
}
