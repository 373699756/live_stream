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
    if (!engine.IsOk()) {
        return 1;
    }
    if (engine.value->Start() != infra::Status::kOk) {
        return 2;
    }
    if (engine.value->Start() != infra::Status::kOk) {
        return 3;
    }

    auto timer = engine.value->RunOnIoAfter(100, []() {});
    if (!timer.IsOk()) {
        return 4;
    }
    if (engine.value->CancelIoTimer(timer.value) != infra::Status::kOk) {
        return 5;
    }
    if (engine.value->CancelIoTimer(timer.value) != infra::Status::kNotFound) {
        return 6;
    }

    live_stream::TcpListenOptions listen;
    listen.address.ip = "127.0.0.1";
    listen.address.port = 0;
    live_stream::TcpCallbacks tcp_callbacks;
    tcp_callbacks.on_accept = OnAccept;
    auto server = engine.value->ListenTcp(listen, tcp_callbacks);
    if (!server.IsOk()) {
        return 7;
    }
    if (!engine.value->TcpLocalAddress(server.value).IsOk()) {
        return 8;
    }
    if (engine.value->CloseTcp(server.value) != infra::Status::kOk) {
        return 9;
    }
    if (engine.value->TcpLocalAddress(server.value).status !=
        infra::Status::kNotFound) {
        return 10;
    }

    live_stream::UdpBindOptions bind;
    bind.address.ip = "127.0.0.1";
    bind.address.port = 0;
    live_stream::UdpCallbacks udp_callbacks;
    udp_callbacks.on_read = OnUdp;
    auto socket_id = engine.value->BindUdp(bind, udp_callbacks);
    if (!socket_id.IsOk()) {
        return 11;
    }
    if (!engine.value->UdpLocalAddress(socket_id.value).IsOk()) {
        return 12;
    }
    if (engine.value->CloseUdp(socket_id.value) != infra::Status::kOk) {
        return 13;
    }
    if (engine.value->UdpLocalAddress(socket_id.value).status !=
        infra::Status::kNotFound) {
        return 14;
    }

    engine.value->Stop();
    engine.value->Stop();
    return 0;
}
