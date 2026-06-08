#include "net.h"

int main() {
    live_stream::NetEngineOptions engine_options;
    live_stream::TcpListenOptions tcp_options;
    live_stream::UdpBindOptions udp_options;
    live_stream::TcpCallbacks tcp_callbacks;
    live_stream::UdpCallbacks udp_callbacks;
    live_stream::NetStats stats;
    (void)engine_options;
    (void)tcp_options;
    (void)udp_options;
    (void)tcp_callbacks;
    (void)udp_callbacks;
    (void)stats;
    return 0;
}
