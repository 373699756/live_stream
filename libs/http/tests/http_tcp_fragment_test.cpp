#include "http.h"

#include "auth.h"
#include "config.h"
#include "socket_io.h"
#include "runtime.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>

namespace {

class FakeAuth : public live_stream::IAuth {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }
    bool SetAuditSink(live_stream::IAuthAuditSink*) override { return true; }

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
        return result;
    }

    bool Logout(const live_stream::RequestContext&) override { return true; }

    live_stream::TokenValidationResult ValidateToken(
        const std::string&) override {
        return live_stream::TokenValidationResult();
    }

    bool ChangePassword(
        const live_stream::ChangePasswordRequest&) override {
        return true;
    }

    bool CheckPermission(const live_stream::AuthPrincipal& principal,
                         live_stream::AuthPermission permission,
                         const std::string&) override {
        if (principal.role == live_stream::AuthRole::kAdmin) {
            return true;
        }
        return permission == live_stream::AuthPermission::kReadStatus;
    }
};

class FakeConfig : public live_stream::IConfig {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }
    live_stream::ConfigCode Set(const std::string&,
                                  const live_stream::Json&,
                                  live_stream::ConfigError*) override {
        return live_stream::ConfigCode::kOk;
    }
    live_stream::Json Get(const std::string&) override {
        return live_stream::Json::object();
    }
    live_stream::ConfigCode Reset(
        const std::string&, live_stream::ConfigError*) override {
        return live_stream::ConfigCode::kOk;
    }
    live_stream::Json Default(const std::string&) override {
        return live_stream::Json::object();
    }
    live_stream::ConfigCode ResetAll(
        live_stream::ConfigError*) override {
        return live_stream::ConfigCode::kOk;
    }
    bool AddScope(const std::string&,
                  const live_stream::ConfigScope&) override {
        return true;
    }
    bool RemoveScope(const std::string&) override { return true; }
};

int ConnectTo(const live_stream::HttpListenAddress& address) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout{};
    timeout.tv_sec = 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
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
        const ssize_t n = send(fd, data.data() + offset, data.size() - offset,
                               MSG_NOSIGNAL);
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
    auto socket_io = live_stream::CreateSocketIo(live_stream::SocketIoOptions{});
    if (!socket_io) {
        return 1;
    }
    if (!socket_io->Start()) {
        return 1;
    }

    FakeAuth auth;
    FakeConfig config;
    live_stream::HttpOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    live_stream::Runtime::Clear();
    (void)live_stream::Runtime::InstallSocketIo(socket_io.get());
    (void)live_stream::Runtime::InstallAuth(&auth);
    (void)live_stream::Runtime::InstallConfig(&config);
    auto http = live_stream::CreateHttp(
        options, socket_io->DefaultLoop(), nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!http || !http->Start()) {
        return 2;
    }

    live_stream::HttpListenAddress address = http->LocalAddress();
    if (address.port == 0) {
        http->Stop();
        return 3;
    }

    const int fd = ConnectTo(address);
    if (fd < 0) {
        http->Stop();
        return 4;
    }

    const std::string body = "{\"user_name\":\"admin\",\"password\":\"pass\"}";
    const std::string prefix =
        "POST /api/auth/login HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: ";
    const std::string suffix = std::to_string(body.size()) + "\r\n\r\n" + body;
    if (!SendAll(fd, prefix) || !SendAll(fd, suffix)) {
        close(fd);
        http->Stop();
        return 5;
    }

    const std::string response = ReadUntilClose(fd);
    close(fd);
    http->Stop();
    socket_io->Stop();
    live_stream::Runtime::Clear();

    if (!Contains(response, "HTTP/1.1 200 OK") ||
        !Contains(response, "live_stream_token=admin-token") ||
        Contains(response, "\"token\"")) {
        return 6;
    }
    return 0;
}
