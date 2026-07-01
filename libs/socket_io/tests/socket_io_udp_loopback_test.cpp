#include "socket_io.h"

#include "infra/time.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace {

struct UdpState {
    bool received = false;
};

void OnUdp(void* user,
           live_stream::UdpSocketId,
           live_stream::SocketAddress,
           const uint8_t* data,
           size_t size) {
    static_cast<UdpState*>(user)->received =
        size == 4 && std::memcmp(data, "pong", 4) == 0;
}

}  // namespace

int main() {
    auto engine = live_stream::CreateSocketIo(live_stream::SocketIoOptions{});
    if (!engine) {
        return 1;
    }
    if (!engine->Start()) {
        return 2;
    }
    live_stream::event::Loop* loop = engine->DefaultLoop();
    if (loop == nullptr) {
        return 3;
    }
    UdpState state;
    live_stream::UdpBindOptions bind;
    bind.address.ip = "127.0.0.1";
    bind.address.port = 0;
    live_stream::UdpCallbacks callbacks;
    callbacks.user = &state;
    callbacks.on_read = OnUdp;
    live_stream::UdpSocketId socket_id =
        engine->BindUdp(loop, bind, callbacks);
    if (socket_id == 0) {
        return 4;
    }
    live_stream::SocketAddress local = engine->UdpLocalAddress(socket_id);
    if (local.port == 0) {
        return 5;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    (void)sendto(fd, "pong", 4, 0, reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr));
    for (int i = 0; i < 20 && !state.received; ++i) {
        infra::Time::SleepMillis(10);
    }
    close(fd);
    engine->Stop();
    return state.received ? 0 : 6;
}
