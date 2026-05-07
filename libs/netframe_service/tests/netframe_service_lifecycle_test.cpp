#include "netframe_service.h"

namespace {

void OnAccept(void*, live_stream::ConnectionId, live_stream::NetAddress) {}

void OnUdp(void*,
           live_stream::UdpSocketId,
           live_stream::NetAddress,
           const uint8_t*,
           size_t) {}

}  // namespace

int main() {
    live_stream::NetEngineOptions options;
    auto engine = live_stream::CreateNetEngine(options);
    if (!engine) {
        return 1;
    }
    if (!engine->Start()) {
        return 2;
    }
    if (!engine->Start()) {
        return 3;
    }

    const live_stream::NetTimerId timer = engine->RunOnIoAfter(100, []() {});
    if (timer == 0) {
        return 4;
    }
    if (!engine->CancelIoTimer(timer)) {
        return 5;
    }
    if (engine->CancelIoTimer(timer)) {
        return 6;
    }

    live_stream::TcpListenOptions listen;
    listen.address.ip = "127.0.0.1";
    listen.address.port = 0;
    live_stream::TcpCallbacks tcp_callbacks;
    tcp_callbacks.on_accept = OnAccept;
    const live_stream::TcpServerId server = engine->ListenTcp(listen, tcp_callbacks);
    if (server == 0) {
        return 7;
    }
    if (engine->TcpLocalAddress(server).port == 0) {
        return 8;
    }
    if (!engine->CloseTcp(server)) {
        return 9;
    }
    if (engine->TcpLocalAddress(server).port != 0) {
        return 10;
    }

    live_stream::UdpBindOptions bind;
    bind.address.ip = "127.0.0.1";
    bind.address.port = 0;
    live_stream::UdpCallbacks udp_callbacks;
    udp_callbacks.on_read = OnUdp;
    const live_stream::UdpSocketId socket_id = engine->BindUdp(bind, udp_callbacks);
    if (socket_id == 0) {
        return 11;
    }
    if (engine->UdpLocalAddress(socket_id).port == 0) {
        return 12;
    }
    if (!engine->CloseUdp(socket_id)) {
        return 13;
    }
    if (engine->UdpLocalAddress(socket_id).port != 0) {
        return 14;
    }

    engine->Stop();
    engine->Stop();
    return 0;
}
