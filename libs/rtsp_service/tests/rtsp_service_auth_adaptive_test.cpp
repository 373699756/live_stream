#include "rtsp_service.h"

#include "auth_service.h"
#include "infra/media_buffer.h"
#include "netframe_service.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
        if (request.user_name != "viewer" || request.password != "pass") {
            return infra::Result<live_stream::LoginResult>::Fail(
                infra::Status::kUnauthorized);
        }
        live_stream::LoginResult result;
        result.principal.user_name = request.user_name;
        result.principal.role = live_stream::AuthRole::kViewer;
        return infra::Result<live_stream::LoginResult>::Ok(result);
    }
    infra::Status Logout(const infra::RequestContext&) override {
        return infra::Status::kOk;
    }
    infra::Result<live_stream::TokenValidationResult> ValidateToken(
        const std::string&) override {
        return infra::Result<live_stream::TokenValidationResult>::Fail(
            infra::Status::kUnauthorized);
    }
    infra::Status CheckPermission(const live_stream::AuthPrincipal&,
                                 live_stream::AuthPermission permission,
                                 const std::string&) override {
        return permission == live_stream::AuthPermission::kPreviewVideo
            ? infra::Status::kOk
            : infra::Status::kNoPermission;
    }
};

class FakeAdaptiveObserver : public live_stream::IRtspAdaptiveObserver {
 public:
    live_stream::RtspAdaptiveAction OnRtspAdaptiveSample(
        const live_stream::RtspAdaptiveSample& sample) override {
        ++samples;
        last_event = sample.event;
        live_stream::RtspAdaptiveAction action;
        return action;
    }
    int samples = 0;
    live_stream::RtspAdaptiveEventType last_event =
        live_stream::RtspAdaptiveEventType::kSample;
};

int ConnectTcp(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout {};
    timeout.tv_sec = 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool SendAndRead(int fd,
                 const std::string& request,
                 const std::string& expected) {
    if (send(fd, request.data(), request.size(), 0) !=
        static_cast<ssize_t>(request.size())) {
        return false;
    }
    std::string response;
    char buffer[512];
    for (int i = 0; i < 20; ++i) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        response.append(buffer, static_cast<size_t>(n));
        if (response.find(expected) != std::string::npos) {
            return true;
        }
    }
    return false;
}

infra::EncodedFrame MakeFrame() {
    auto buffer = infra::CreateMediaBuffer(3);
    uint8_t* data = buffer->MutableData();
    data[0] = 0x65;
    data[1] = 1;
    data[2] = 2;
    buffer->SetSize(3);
    infra::EncodedFrame frame;
    frame.stream_id = infra::StreamId::kMain;
    frame.codec = infra::VideoCodec::kH264;
    frame.frame_type = infra::FrameType::kIdr;
    frame.pts_us = 300000;
    frame.buffer = buffer;
    frame.size = 3;
    return frame;
}

}  // namespace

int main() {
    auto netframe = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!netframe.IsOk()) {
        return 1;
    }
    FakeAuthService auth;
    FakeAdaptiveObserver adaptive;
    live_stream::RtspServiceOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;
    options.enable_auth = true;
    live_stream::RtspServiceDependencies deps;
    deps.net_engine = netframe.value.get();
    deps.auth_service = &auth;
    deps.adaptive_observer = &adaptive;
    auto rtsp = live_stream::CreateRtspService(options, deps);
    if (!rtsp || rtsp->Start() != infra::Status::kOk) {
        return 2;
    }
    const auto local = rtsp->LocalAddress();
    if (!local.IsOk()) {
        return 3;
    }
    int fd = ConnectTcp(local.value.port);
    if (fd < 0) {
        return 4;
    }
    if (!SendAndRead(fd, "DESCRIBE rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                         "CSeq: 1\r\n\r\n",
                     "401 Unauthorized")) {
        close(fd);
        return 5;
    }
    close(fd);

    fd = ConnectTcp(local.value.port);
    if (fd < 0) {
        return 6;
    }
    const char* auth_header = "Authorization: Basic dmlld2VyOnBhc3M=\r\n";
    if (!SendAndRead(fd, std::string("DESCRIBE rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                                     "CSeq: 2\r\n") +
                         auth_header + "\r\n",
                     "application/sdp") ||
        !SendAndRead(fd, std::string("SETUP rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                                     "CSeq: 3\r\n") +
                         auth_header +
                         "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n",
                     "interleaved=0-1") ||
        !SendAndRead(fd, std::string("PLAY rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                                     "CSeq: 4\r\n") +
                         auth_header + "\r\n",
                     "200 OK")) {
        close(fd);
        return 7;
    }
    if (rtsp->PushFrame(MakeFrame()) != infra::Status::kOk) {
        close(fd);
        return 8;
    }
    char buffer[256];
    if (recv(fd, buffer, sizeof(buffer), 0) <= 0 || adaptive.samples == 0) {
        close(fd);
        return 9;
    }
    if (rtsp->GetStats().auth_failures == 0 ||
        rtsp->GetStats().tcp_interleaved_sessions == 0) {
        close(fd);
        return 10;
    }

    close(fd);
    rtsp->Deinit();
    netframe->Deinit();
    return 0;
}
