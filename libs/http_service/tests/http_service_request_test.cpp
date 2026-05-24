#include "http_console_service.h"
#include "http_service.h"
#include "http_service_dependencies.h"

#include "auth_service.h"
#include "config_service.h"
#include "logger_service.h"
#include "media_service.h"
#include "net_service.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeAuthService : public live_stream::IAuthService {
public:
  bool Start() override { return true; }
  void Stop() override {}
  bool IsStarted() const override { return true; }
  bool SetAuditSink(live_stream::IAuthAuditSink*) override { return true; }

  live_stream::LoginResult Login(
      const live_stream::LoginRequest& request) override {
    live_stream::LoginResult result;
    if (request.user_name == "admin" && request.password == "pass") {
      result.principal.user_name = "admin";
      result.principal.session_id = "session-1";
      result.principal.role = live_stream::AuthRole::kAdmin;
      result.token = "admin-token";
    }
    return result;
  }

  bool Logout(const live_stream::RequestContext&) override { return true; }
  live_stream::TokenValidationResult ValidateToken(
      const std::string& token) override {
    live_stream::TokenValidationResult result;
    if (token == "admin-token") {
      result.principal.user_name = "admin";
      result.principal.session_id = "session-1";
      result.principal.role = live_stream::AuthRole::kAdmin;
    }
    return result;
  }
  bool ChangePassword(
      const live_stream::ChangePasswordRequest&) override {
    return true;
  }
  bool CheckPermission(const live_stream::AuthPrincipal& principal,
                       live_stream::AuthPermission permission,
                       const std::string&) override {
    return principal.role == live_stream::AuthRole::kAdmin ||
           permission == live_stream::AuthPermission::kReadStatus ||
           permission == live_stream::AuthPermission::kPreviewVideo;
  }
};

class FakeConfigService : public live_stream::IConfigService {
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

class FakeLoggerService : public live_stream::ILoggerService {
public:
  bool Start() override { return true; }
  void Stop() override {}
  bool IsStarted() const override { return true; }
  bool RecordOperation(const live_stream::OperationRecord& record) override {
    records.push_back(record);
    return true;
  }
  std::vector<live_stream::OperationRecord> QueryOperations(
      const live_stream::OperationLogQuery&) override {
    return records;
  }
  bool ExportOperations(
      const live_stream::OperationLogExportOptions&) override {
    return true;
  }

  std::vector<live_stream::OperationRecord> records;
};

class FakeNetEngine : public live_stream::NetEngine {
public:
  bool Start() override { return true; }
  void Stop() override {}

  live_stream::TcpServerId ListenTcp(
      const live_stream::TcpListenOptions&,
      const live_stream::TcpCallbacks&) override {
    return 1;
  }

  bool CloseTcp(live_stream::TcpServerId) override { return true; }

  live_stream::UdpSocketId BindUdp(
      const live_stream::UdpBindOptions&,
      const live_stream::UdpCallbacks&) override {
    return 1;
  }

  bool CloseUdp(live_stream::UdpSocketId) override { return true; }

  bool Send(live_stream::ConnectionId, const uint8_t*, size_t) override {
    return true;
  }

  bool Close(live_stream::ConnectionId) override { return true; }
  bool CloseAfterSend(live_stream::ConnectionId) override { return true; }
  bool SendTo(live_stream::UdpSocketId, live_stream::NetAddress,
              const uint8_t*, size_t) override {
    return true;
  }

  live_stream::NetTimerId RunOnIoAfter(uint32_t, infra::Task) override {
    return 1;
  }

  bool CancelIoTimer(live_stream::NetTimerId) override { return true; }

  live_stream::NetAddress TcpLocalAddress(
      live_stream::TcpServerId) const override {
    return live_stream::NetAddress{"127.0.0.1", 8080};
  }

  live_stream::NetAddress UdpLocalAddress(
      live_stream::UdpSocketId) const override {
    return live_stream::NetAddress{"127.0.0.1", 3702};
  }

  uint32_t PendingBytes(live_stream::ConnectionId) const override { return 0; }

  live_stream::NetStats GetStats() const override {
    return live_stream::NetStats();
  }
};

class FakeMediaService : public live_stream::IMediaService {
public:
  bool Start() override { return true; }
  void Stop() override {}
  bool IsStarted() const override { return true; }
  bool IsRestarting() const override { return false; }
  bool IsStreamStarted(live_stream::StreamId) const override { return true; }
  live_stream::VideoCodec GetStreamCodec(
      live_stream::StreamId) const override {
    return live_stream::VideoCodec::kH264;
  }
  live_stream::FrameAttachId AttachFrameSink(
      const live_stream::FrameAttachOptions&,
      live_stream::IFrameSink*) override {
    return 0;
  }
  bool DetachFrameSink(live_stream::FrameAttachId) override { return true; }
  bool RequestKeyFrame(live_stream::StreamId,
                       live_stream::KeyFrameReason) override {
    return true;
  }
  live_stream::MediaCapabilities GetCapabilities() const override {
    live_stream::MediaCapabilities capabilities;
    live_stream::VideoStreamCapabilities main;
    main.stream_id = live_stream::StreamId::kMain;
    main.codecs.push_back(
        live_stream::CodecCapability{live_stream::VideoCodec::kH264, {}});
    main.resolutions.push_back({1920, 1080});
    capabilities.streams.push_back(main);
    capabilities.image.basic.push_back({"brightness", 0, 100, 50, true});
    return capabilities;
  }
  live_stream::MediaChannels GetChannels() const override {
    live_stream::MediaChannels channels;
    channels.main_size = live_stream::VideoSize{1920, 1080};
    channels.sub_size = live_stream::VideoSize{640, 360};
    return channels;
  }
  live_stream::ImageStrategyStatus GetImageStrategyStatus() const override {
    return live_stream::ImageStrategyStatus();
  }
};

live_stream::HttpRequest Request(live_stream::HttpMethod method,
                                 const std::string& path,
                                 const std::string& body,
                                 const std::string& token) {
  live_stream::HttpRequest request;
  request.method = method;
  request.path = path;
  request.body = body;
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
  FakeMediaService media;
  FakeNetEngine net_engine;

  live_stream::HttpServiceOptions options;
  live_stream::HttpServiceDependencies deps;
  deps.net_engine = nullptr;

  std::unique_ptr<live_stream::IHttpService> base =
      live_stream::CreateHttpService(options, deps);
  if (!base || base->HandleRequest(Request(live_stream::HttpMethod::kGet,
                                           "bad", "", "")).status_code != 500) {
    return 1;
  }

  std::unique_ptr<live_stream::IHttpService> console =
      live_stream::CreateHttpConsoleService(
          options, &net_engine, &auth, &logger, &config, nullptr, nullptr,
          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &media,
          nullptr, nullptr, nullptr, nullptr);
  if (!console || !console->Start()) {
    return 2;
  }

  live_stream::HttpResponse login =
      console->HandleRequest(Request(live_stream::HttpMethod::kPost,
                                     "/api/auth/login",
                                     "{\"user_name\":\"admin\",\"password\":\"pass\"}",
                                     ""));
  if (login.status_code != 200 || !Contains(login.body, "admin-token")) {
    return 3;
  }

  live_stream::HttpResponse config_get =
      console->HandleRequest(Request(live_stream::HttpMethod::kGet,
                                     "/api/config/video",
                                     "",
                                     "admin-token"));
  if (config_get.status_code != 200) {
    return 4;
  }

  live_stream::HttpResponse media_caps =
      console->HandleRequest(Request(live_stream::HttpMethod::kGet,
                                     "/api/media/capabilities",
                                     "",
                                     "admin-token"));
  if (media_caps.status_code != 200 ||
      !Contains(media_caps.body, "\"streams\"") ||
      !Contains(media_caps.body, "1920")) {
    return 5;
  }

  live_stream::HttpResponse snapshot =
      console->HandleRequest(Request(live_stream::HttpMethod::kGet,
                                     "/api/snapshot/main.jpg",
                                     "",
                                     "admin-token"));
  if (snapshot.status_code != 501) {
    return 6;
  }

  live_stream::HttpResponse not_impl =
      console->HandleRequest(Request(live_stream::HttpMethod::kPost,
                                     "/api/upgrade",
                                     "",
                                     "admin-token"));
  if (not_impl.status_code != 501) {
    return 7;
  }

  console->Stop();
  return 0;
}
