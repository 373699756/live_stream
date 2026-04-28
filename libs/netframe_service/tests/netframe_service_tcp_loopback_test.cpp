#include "netframe_service.h"

#include "infra/time.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace {

struct TcpState {
    live_stream::NetEngine* engine = nullptr;
    live_stream::ConnectionId connection_id = 0;
    bool received = false;
};

void OnAccept(void* user,
              live_stream::ConnectionId id,
              live_stream::NetAddress) {
    static_cast<TcpState*>(user)->connection_id = id;
}

void OnRead(void* user,
            live_stream::ConnectionId id,
            const uint8_t* data,
            size_t size) {
    TcpState* state = static_cast<TcpState*>(user);
    state->received = size == 4 && std::memcmp(data, "ping", 4) == 0;
    (void)state->engine->Send(id, data, size);
    (void)state->engine->CloseAfterSend(id);
}

}  // namespace

int main() {
    live_stream::NetEngineOptions engine_options;
    auto engine = live_stream::CreateNetEngine(engine_options);
    if (!engine.IsOk()) {
        return 1;
    }

    TcpState state;
    state.engine = engine.value.get();
    live_stream::TcpListenOptions listen;
    listen.address.ip = "127.0.0.1";
    listen.address.port = 0;
    live_stream::TcpCallbacks callbacks;
    callbacks.user = &state;
    callbacks.on_accept = OnAccept;
    callbacks.on_read = OnRead;
    auto server = engine.value->ListenTcp(listen, callbacks);
    if (!server.IsOk() || engine.value->Start() != infra::Status::kOk) {
        return 2;
    }
    auto local = engine.value->TcpLocalAddress(server.value);
    if (!local.IsOk()) {
        return 3;
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.value.port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return 4;
    }
    if (send(fd, "ping", 4, 0) != 4) {
        close(fd);
        return 5;
    }
    char buffer[8] = {};
    const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
    close(fd);
    engine.value->Stop();
    return n == 4 && std::memcmp(buffer, "ping", 4) == 0 && state.received
               ? 0
               : 6;
}
