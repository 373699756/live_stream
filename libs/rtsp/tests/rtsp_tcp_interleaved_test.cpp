#include "rtsp.h"

#include "fake_media_source.h"
#include "media/media_buffer.h"
#include "net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

namespace {

int ConnectTo(const live_stream::RtspListenAddress& address) {
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
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool SendAll(int fd, const std::string& data) {
    return send(fd, data.data(), data.size(), 0) ==
           static_cast<ssize_t>(data.size());
}

bool ReadUntil(int fd, const std::string& needle, std::string* out) {
    char buffer[512];
    for (int i = 0; i < 20; ++i) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        out->append(buffer, static_cast<size_t>(n));
        if (out->find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

live_stream::EncodedFrame MakeFrame() {
    live_stream::VideoBuffer* buffer = live_stream::VideoBufferAlloc(5);
    uint8_t* data = buffer->data;
    data[0] = 0x65;
    data[1] = 1;
    data[2] = 2;
    data[3] = 3;
    data[4] = 4;
    (void)live_stream::VideoBufferSetSize(buffer, 5);
    live_stream::EncodedFrame frame;
    frame.stream_id = live_stream::StreamId::kMain;
    frame.codec = live_stream::VideoCodec::kH264;
    frame.frame_type = live_stream::FrameType::kIdr;
    frame.pts_us = 100000;
    frame.dts_us = 100000;
    frame.buffer = buffer;
    frame.size = 5;
    return frame;
}

}  // namespace

int main() {
    std::unique_ptr<live_stream::INetEngine> net_engine =
        live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!net_engine || !net_engine->Start()) {
        return 1;
    }

    live_stream::test::FakeMediaFrameSource media_source;
    live_stream::RtspOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;
    live_stream::RtspDependencies deps;
    deps.net_engine = net_engine.get();
    deps.net_executor = net_engine->DefaultExecutor();
    deps.media_source = &media_source;
    auto rtsp = live_stream::CreateRtsp(options, deps);
    if (!rtsp || !rtsp->Start()) {
        return 2;
    }
    const auto local = rtsp->LocalAddress();
    if (local.port == 0) {
        return 3;
    }
    const int fd = ConnectTo(local);
    if (fd < 0) {
        return 4;
    }

    std::string received;
    if (!SendAll(fd,
                 "OPTIONS rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                 "CSeq: 1\r\n\r\n") ||
        !ReadUntil(fd, "200 OK", &received)) {
        close(fd);
        return 5;
    }
    received.clear();
    if (!SendAll(fd,
                 "DESCRIBE rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                 "CSeq: 2\r\n\r\n") ||
        !ReadUntil(fd, "application/sdp", &received)) {
        close(fd);
        return 6;
    }
    received.clear();
    if (!SendAll(fd,
                 "SETUP rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                 "CSeq: 3\r\n"
                 "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n") ||
        !ReadUntil(fd, "interleaved=0-1", &received)) {
        close(fd);
        return 7;
    }
    received.clear();
    if (!SendAll(fd,
                 "PLAY rtsp://127.0.0.1/live/main RTSP/1.0\r\n"
                 "CSeq: 4\r\n\r\n") ||
        !ReadUntil(fd, "200 OK", &received)) {
        close(fd);
        return 8;
    }

    if (!media_source.DeliverFrame(MakeFrame())) {
        close(fd);
        return 9;
    }
    received.clear();
    if (!ReadUntil(fd, "$", &received)) {
        close(fd);
        return 10;
    }
    if (received.find('$') == std::string::npos ||
        received.find(static_cast<char>(0x80 | 96)) == std::string::npos) {
        close(fd);
        return 11;
    }
    if (rtsp->GetStats().sent_rtp_packets == 0) {
        close(fd);
        return 12;
    }

    close(fd);
    rtsp->Stop();
    net_engine->Stop();
    return 0;
}
