#include "http.h"
#include "http_dependencies.h"
#include "http_console.h"

#include "auth.h"
#include "config.h"
#include "net.h"

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
  bool SetValue(const std::string&, const live_stream::ConfigJson&) override {
    return true;
  }
  live_stream::ConfigJson GetValue(const std::string&) override {
    return live_stream::ConfigJson::object();
  }
  bool SetDefault(const std::string&) override { return true; }
  live_stream::ConfigJson GetDefault(const std::string&) override {
    return live_stream::ConfigJson::object();
  }
  bool RestoreDefaults() override { return true; }
  bool AttachConfig(const std::string&,
                    const live_stream::ConfigAttachment&) override {
    return true;
  }
  bool DetachConfig(const std::string&) override { return true; }
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
  auto net_engine = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
  if (!net_engine) {
    return 1;
  }
  if (!net_engine->Start()) {
    return 1;
  }

  FakeAuth auth;
  FakeConfig config;
  live_stream::HttpDependencies deps;
  deps.net_engine = net_engine.get();

  live_stream::HttpOptions options;
  options.listen_ip = "127.0.0.1";
  options.listen_port = 0;
  options.enable_keep_alive = true;
  options.max_requests_per_connection = 2;
  options.max_pipelined_requests = 2;
  options.stream_executor_worker_count = 1;
  options.control_executor_worker_count = 1;

  live_stream::HttpConsoleDependencies console_deps;
  console_deps.net_engine = net_engine.get();
  console_deps.auth = &auth;
  console_deps.config = &config;
  auto http = live_stream::CreateHttpConsole(options, console_deps);
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
  const std::string request =
      "POST /api/auth/login HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
      "Content-Length: " +
      std::to_string(body.size()) + "\r\n\r\n" + body;
  if (!SendAll(fd, request + request)) {
    close(fd);
    http->Stop();
    return 5;
  }

  const std::string response = ReadUntilClose(fd);
  close(fd);
  http->Stop();
  net_engine->Stop();

  if (Count(response, "HTTP/1.1 200 OK") != 2 ||
      Count(response, "admin-token") != 2 ||
      response.find("Connection: keep-alive") == std::string::npos ||
      response.find("Connection: close") == std::string::npos) {
    return 6;
  }
  return 0;
}
