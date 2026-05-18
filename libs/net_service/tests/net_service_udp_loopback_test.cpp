#include "net_service.h"

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
           live_stream::NetAddress,
           const uint8_t* data,
           size_t size) {
    static_cast<UdpState*>(user)->received =
        size == 4 && std::memcmp(data, "pong", 4) == 0;
}

}  // namespace

int main() {
    auto engine = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!engine.IsOk()) {
        return 1;
    }
    UdpState state;
    live_stream::UdpBindOptions bind;
    bind.address.ip = "127.0.0.1";
    bind.address.port = 0;
    live_stream::UdpCallbacks callbacks;
    callbacks.user = &state;
    callbacks.on_read = OnUdp;
    auto socket_id = engine.value->BindUdp(bind, callbacks);
    if (!socket_id.IsOk() || engine.value->Start() != infra::Status::kOk) {
        return 2;
    }
    auto local = engine.value->UdpLocalAddress(socket_id.value);
    if (!local.IsOk()) {
        return 3;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.value.port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    (void)sendto(fd, "pong", 4, 0, reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr));
    for (int i = 0; i < 20 && !state.received; ++i) {
        infra::Time::SleepMillis(10);
    }
    close(fd);
    engine.value->Stop();
    return state.received ? 0 : 4;
}
