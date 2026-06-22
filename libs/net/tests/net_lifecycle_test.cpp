#include "net.h"

namespace {

void OnAccept(void *, live_stream::ConnectionId, live_stream::NetAddress) {}

void OnUdp(void *,
           live_stream::UdpSocketId,
           live_stream::NetAddress,
           const uint8_t *,
           size_t) {}

}  // namespace

int main() {
    live_stream::NetEngineOptions options;
    options.io_threads = 2;
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

    live_stream::event::Loop *loop = engine->DefaultLoop();
    if (loop == nullptr) {
        return 4;
    }
    live_stream::event::Loop *picked_loop = engine->PickLoop();
    if (picked_loop == nullptr) {
        return 5;
    }

    live_stream::event::TimerId timer = 0;
    if (loop->RunAfter(100, []() {}, &timer) !=
        live_stream::event::EventStatus::kOk) {
        return 6;
    }
    if (timer == 0) {
        return 7;
    }
    if (!loop->CancelTimer(timer)) {
        return 8;
    }
    if (loop->CancelTimer(timer)) {
        return 9;
    }

    live_stream::event::Loop external_loop;
    live_stream::TcpListenOptions invalid_listen;
    invalid_listen.address.ip = "127.0.0.1";
    invalid_listen.address.port = 0;
    live_stream::TcpCallbacks invalid_tcp_callbacks;
    invalid_tcp_callbacks.on_accept = OnAccept;
    if (engine->ListenTcp(nullptr, invalid_listen, invalid_tcp_callbacks) != 0) {
        return 10;
    }
    if (engine->ListenTcp(&external_loop, invalid_listen,
                          invalid_tcp_callbacks) != 0) {
        return 11;
    }

    live_stream::UdpBindOptions invalid_bind;
    invalid_bind.address.ip = "127.0.0.1";
    invalid_bind.address.port = 0;
    live_stream::UdpCallbacks invalid_udp_callbacks;
    invalid_udp_callbacks.on_read = OnUdp;
    if (engine->BindUdp(nullptr, invalid_bind, invalid_udp_callbacks) != 0) {
        return 12;
    }
    if (engine->BindUdp(&external_loop, invalid_bind,
                        invalid_udp_callbacks) != 0) {
        return 13;
    }

    live_stream::TcpListenOptions listen;
    listen.address.ip = "127.0.0.1";
    listen.address.port = 0;
    live_stream::TcpCallbacks tcp_callbacks;
    tcp_callbacks.on_accept = OnAccept;
    const live_stream::TcpServerId server =
        engine->ListenTcp(loop, listen, tcp_callbacks);
    if (server == 0) {
        return 14;
    }
    if (engine->TcpLocalAddress(server).port == 0) {
        return 15;
    }
    if (!engine->CloseTcp(server)) {
        return 16;
    }
    if (engine->TcpLocalAddress(server).port != 0) {
        return 17;
    }

    const live_stream::TcpServerId stop_server =
        engine->ListenTcp(loop, listen, tcp_callbacks);
    if (stop_server == 0) {
        return 18;
    }
    if (engine->TcpLocalAddress(stop_server).port == 0) {
        return 19;
    }

    live_stream::UdpBindOptions bind;
    bind.address.ip = "127.0.0.1";
    bind.address.port = 0;
    live_stream::UdpCallbacks udp_callbacks;
    udp_callbacks.on_read = OnUdp;
    const live_stream::UdpSocketId socket_id =
        engine->BindUdp(loop, bind, udp_callbacks);
    if (socket_id == 0) {
        return 20;
    }
    if (engine->UdpLocalAddress(socket_id).port == 0) {
        return 21;
    }
    if (!engine->CloseUdp(socket_id)) {
        return 22;
    }
    if (engine->UdpLocalAddress(socket_id).port != 0) {
        return 23;
    }

    const live_stream::UdpSocketId stop_socket_id =
        engine->BindUdp(loop, bind, udp_callbacks);
    if (stop_socket_id == 0) {
        return 24;
    }
    if (engine->UdpLocalAddress(stop_socket_id).port == 0) {
        return 25;
    }

    engine->Stop();
    if (engine->TcpLocalAddress(stop_server).port != 0) {
        return 26;
    }
    if (engine->UdpLocalAddress(stop_socket_id).port != 0) {
        return 27;
    }
    engine->Stop();
    return 0;
}
