#include "socket_io.h"

#include "infra/time.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace {

struct TcpState {
    live_stream::ISocketIo* engine = nullptr;
    live_stream::ConnectionId connection_id = 0;
    bool received = false;
};

void OnAccept(void* user,
              live_stream::ConnectionId id,
              live_stream::SocketAddress) {
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
    live_stream::SocketIoOptions engine_options;
    auto engine = live_stream::CreateSocketIo(engine_options);
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

    TcpState state;
    state.engine = engine.get();
    live_stream::TcpListenOptions listen;
    listen.address.ip = "127.0.0.1";
    listen.address.port = 0;
    live_stream::TcpCallbacks callbacks;
    callbacks.user = &state;
    callbacks.on_accept = OnAccept;
    callbacks.on_read = OnRead;
    live_stream::TcpServerId server =
        engine->ListenTcp(loop, listen, callbacks);
    if (server == 0) {
        return 4;
    }
    live_stream::SocketAddress local = engine->TcpLocalAddress(server);
    if (local.port == 0) {
        return 5;
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return 6;
    }
    if (send(fd, "ping", 4, 0) != 4) {
        close(fd);
        return 7;
    }
    char buffer[8] = {};
    const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
    close(fd);
    engine->Stop();
    return n == 4 && std::memcmp(buffer, "ping", 4) == 0 && state.received
               ? 0
               : 8;
}
