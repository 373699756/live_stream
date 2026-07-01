#include "rtsp.h"

#include "fake_media_streams.h"
#include "media/media_buffer.h"
#include "net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

namespace {

int BindUdp(uint16_t* port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout{};
    timeout.tv_sec = 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        close(fd);
        return -1;
    }
    *port = ntohs(addr.sin_port);
    return fd;
}

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

bool Exchange(int fd, const std::string& request, const std::string& expected) {
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

live_stream::MediaFrame MakeFrame() {
    live_stream::MediaBufferBuilder buffer =
        live_stream::MediaBufferBuilder::Allocate(4);
    uint8_t* data = buffer.Data();
    data[0] = 0x65;
    data[1] = 9;
    data[2] = 8;
    data[3] = 7;
    (void)buffer.Resize(4);
    live_stream::MediaFrame frame;
    frame.stream_id = live_stream::StreamId::kMain;
    frame.codec = live_stream::Codec::kH264;
    frame.frame_type = live_stream::FrameType::kIdr;
    frame.pts_us = 200000;
    frame.payload = buffer.Finish();
    return frame;
}

}  // namespace

int main() {
    uint16_t udp_port = 0;
    const int udp_fd = BindUdp(&udp_port);
    if (udp_fd < 0) {
        return 1;
    }

    std::unique_ptr<live_stream::INetIo> net_io =
        live_stream::CreateNetIo(live_stream::NetIoOptions{});
    if (!net_io || !net_io->Start()) {
        close(udp_fd);
        return 2;
    }
    live_stream::test::FakeMediaStreams media_streams;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;
    live_stream::RtspDependencies deps;
    deps.net_io = net_io.get();
    deps.net_loop = net_io->DefaultLoop();
    auto rtsp = live_stream::CreateRtsp(options, deps);
    if (!rtsp || !rtsp->Start()) {
        close(udp_fd);
        return 3;
    }
    const auto local = rtsp->LocalAddress();
    if (local.port == 0) {
        close(udp_fd);
        return 4;
    }
    const int tcp_fd = ConnectTcp(local.port);
    if (tcp_fd < 0) {
        close(udp_fd);
        return 5;
    }

    if (!Exchange(tcp_fd,
                  "DESCRIBE rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                  "CSeq: 1\r\n\r\n",
                  "application/sdp")) {
        close(tcp_fd);
        close(udp_fd);
        return 6;
    }
    const std::string setup =
        "SETUP rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "Transport: RTP/AVP;unicast;client_port=" +
        std::to_string(udp_port) + "-" + std::to_string(udp_port + 1) +
        "\r\n\r\n";
    if (!Exchange(tcp_fd, setup, "client_port=") ||
        !Exchange(tcp_fd,
                  "PLAY rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                  "CSeq: 3\r\n\r\n",
                  "200 OK")) {
        close(tcp_fd);
        close(udp_fd);
        return 7;
    }
    if (!media_streams.DeliverFrame(MakeFrame())) {
        close(tcp_fd);
        close(udp_fd);
        return 8;
    }

    uint8_t packet[256] = {};
    const ssize_t n = recv(udp_fd, packet, sizeof(packet), 0);
    if (n < 16 || packet[0] != 0x80 || (packet[1] & 0x7f) != 96) {
        close(tcp_fd);
        close(udp_fd);
        return 9;
    }
    if (rtsp->GetStats().udp_sessions == 0 ||
        rtsp->GetStats().sent_rtp_packets == 0) {
        close(tcp_fd);
        close(udp_fd);
        return 10;
    }

    close(tcp_fd);
    close(udp_fd);
    rtsp->Stop();
    net_io->Stop();
    return 0;
}
