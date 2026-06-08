#include "rtsp.h"

#include "auth.h"
#include "fake_media_source.h"
#include "media/media_buffer.h"
#include "net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

    bool SetAuditSink(live_stream::IAuthAuditSink*) override {
        return true;
    }

    live_stream::LoginResult Login(
        const live_stream::LoginRequest& request) override {
        if (request.user_name != "viewer" || request.password != "pass") {
            return live_stream::LoginResult{};
        }
        live_stream::LoginResult result;
        result.principal.user_name = request.user_name;
        result.principal.role = live_stream::AuthRole::kViewer;
        result.token = "viewer-token";
        return result;
    }

    bool Logout(const live_stream::RequestContext&) override {
        return true;
    }

    bool ChangePassword(
        const live_stream::ChangePasswordRequest&) override {
        return true;
    }

    live_stream::TokenValidationResult ValidateToken(
        const std::string&) override {
        return live_stream::TokenValidationResult{};
    }

    bool CheckPermission(const live_stream::AuthPrincipal&,
                         live_stream::AuthPermission permission,
                         const std::string&) override {
        return permission == live_stream::AuthPermission::kPreviewVideo
                   ? true
                   : false;
    }
};

int ConnectTcp(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout{};
    timeout.tv_sec = 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
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

live_stream::EncodedFrame MakeFrame() {
    live_stream::VideoBuffer* buffer = live_stream::VideoBufferAlloc(3);
    uint8_t* data = buffer->data;
    data[0] = 0x65;
    data[1] = 1;
    data[2] = 2;
    (void)live_stream::VideoBufferSetSize(buffer, 3);
    live_stream::EncodedFrame frame;
    frame.stream_id = live_stream::StreamId::kMain;
    frame.codec = live_stream::VideoCodec::kH264;
    frame.frame_type = live_stream::FrameType::kIdr;
    frame.pts_us = 300000;
    frame.buffer = buffer;
    frame.size = 3;
    return frame;
}

}  // namespace

int main() {
    std::unique_ptr<live_stream::INetEngine> net_engine =
        live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!net_engine || !net_engine->Start()) {
        return 1;
    }
    FakeAuth auth;
    live_stream::test::FakeMediaFrameSource media_source;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;
    options.enable_auth = true;
    live_stream::RtspDependencies deps;
    deps.net_engine = net_engine.get();
    deps.auth = &auth;
    deps.media_source = &media_source;
    auto rtsp = live_stream::CreateRtsp(options, deps);
    if (!rtsp || !rtsp->Start()) {
        return 2;
    }
    const auto local = rtsp->LocalAddress();
    if (local.port == 0) {
        return 3;
    }
    int fd = ConnectTcp(local.port);
    if (fd < 0) {
        return 4;
    }
    if (!SendAndRead(fd,
                     "DESCRIBE rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                     "CSeq: 1\r\n\r\n",
                     "401 Unauthorized")) {
        close(fd);
        return 5;
    }
    close(fd);

    fd = ConnectTcp(local.port);
    if (fd < 0) {
        return 6;
    }
    const char* auth_header = "Authorization: Basic dmlld2VyOnBhc3M=\r\n";
    if (!SendAndRead(fd, std::string("DESCRIBE "
                                     "rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                                     "CSeq: 2\r\n") +
                             auth_header + "\r\n",
                     "application/sdp") ||
        !SendAndRead(fd,
                     "SETUP rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                     "CSeq: 3\r\n"
                     "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n",
                     "interleaved=0-1") ||
        !SendAndRead(fd,
                     "PLAY rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                     "CSeq: 4\r\n\r\n",
                     "200 OK")) {
        close(fd);
        return 7;
    }
    int unauthenticated_fd = ConnectTcp(local.port);
    if (unauthenticated_fd < 0) {
        close(fd);
        return 8;
    }
    if (!SendAndRead(unauthenticated_fd,
                     "DESCRIBE rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                     "CSeq: 5\r\n\r\n",
                     "401 Unauthorized")) {
        close(unauthenticated_fd);
        close(fd);
        return 9;
    }
    close(unauthenticated_fd);
    if (!media_source.DeliverFrame(MakeFrame())) {
        close(fd);
        return 10;
    }
    char buffer[256];
    if (recv(fd, buffer, sizeof(buffer), 0) <= 0) {
        close(fd);
        return 11;
    }
    if (rtsp->GetStats().auth_failures == 0 ||
        rtsp->GetStats().tcp_interleaved_sessions == 0) {
        close(fd);
        return 12;
    }

    close(fd);
    rtsp->Stop();
    net_engine->Stop();
    return 0;
}
