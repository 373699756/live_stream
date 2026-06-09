#include "net.h"

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

    live_stream::INetExecutor *executor = engine->DefaultExecutor();
    if (executor == nullptr) {
        return 4;
    }

    const live_stream::NetTimerId timer = executor->RunAfter(100, []() {});
    if (timer == 0) {
        return 5;
    }
    if (!executor->CancelTimer(timer)) {
        return 6;
    }
    if (executor->CancelTimer(timer)) {
        return 7;
    }

    live_stream::TcpListenOptions invalid_listen;
    invalid_listen.address.ip = "127.0.0.1";
    invalid_listen.address.port = 0;
    live_stream::TcpCallbacks invalid_tcp_callbacks;
    invalid_tcp_callbacks.on_accept = OnAccept;
    if (engine->ListenTcp(nullptr, invalid_listen, invalid_tcp_callbacks) != 0) {
        return 8;
    }

    live_stream::UdpBindOptions invalid_bind;
    invalid_bind.address.ip = "127.0.0.1";
    invalid_bind.address.port = 0;
    live_stream::UdpCallbacks invalid_udp_callbacks;
    invalid_udp_callbacks.on_read = OnUdp;
    if (engine->BindUdp(nullptr, invalid_bind, invalid_udp_callbacks) != 0) {
        return 9;
    }

    live_stream::TcpListenOptions listen;
    listen.address.ip = "127.0.0.1";
    listen.address.port = 0;
    live_stream::TcpCallbacks tcp_callbacks;
    tcp_callbacks.on_accept = OnAccept;
    const live_stream::TcpServerId server =
        engine->ListenTcp(executor, listen, tcp_callbacks);
    if (server == 0) {
        return 10;
    }
    if (engine->TcpLocalAddress(server).port == 0) {
        return 11;
    }
    if (!engine->CloseTcp(server)) {
        return 12;
    }
    if (engine->TcpLocalAddress(server).port != 0) {
        return 13;
    }

    live_stream::UdpBindOptions bind;
    bind.address.ip = "127.0.0.1";
    bind.address.port = 0;
    live_stream::UdpCallbacks udp_callbacks;
    udp_callbacks.on_read = OnUdp;
    const live_stream::UdpSocketId socket_id =
        engine->BindUdp(executor, bind, udp_callbacks);
    if (socket_id == 0) {
        return 14;
    }
    if (engine->UdpLocalAddress(socket_id).port == 0) {
        return 15;
    }
    if (!engine->CloseUdp(socket_id)) {
        return 16;
    }
    if (engine->UdpLocalAddress(socket_id).port != 0) {
        return 17;
    }

    engine->Stop();
    engine->Stop();
    return 0;
}
