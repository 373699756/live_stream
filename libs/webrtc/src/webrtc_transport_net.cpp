#include "webrtc_transport_net.h"

#include <utility>

namespace live_stream {
namespace webrtc_internal {

namespace {

void OnUdp(void *, UdpSocketId, NetAddress, const uint8_t *, size_t) {}

}  // namespace

NetWebrtcTransport::NetWebrtcTransport(NetEngine *net_engine)
    : net_engine_(net_engine) {}

NetWebrtcTransport::~NetWebrtcTransport() { Stop(); }

bool NetWebrtcTransport::Start(const std::string &listen_ip, uint16_t port) {
    if (socket_id_ != 0) {
        return true;
    }
    if (net_engine_ == nullptr) {
        return false;
    }
    UdpBindOptions options;
    options.address.ip = listen_ip.empty() ? "0.0.0.0" : listen_ip;
    options.address.port = port;
    UdpCallbacks callbacks;
    callbacks.on_read = OnUdp;
    UdpSocketId socket = net_engine_->BindUdp(options, callbacks);
    if (socket == 0) {
        return false;
    }
    socket_id_ = socket;
    if (!net_engine_->Start()) {
        Stop();
        return false;
    }
    return true;
}

void NetWebrtcTransport::Stop() {
    if (net_engine_ != nullptr && socket_id_ != 0) {
        (void)net_engine_->CloseUdp(socket_id_);
    }
    socket_id_ = 0;
}

bool NetWebrtcTransport::SendTo(NetAddress address, const uint8_t *data,
                                size_t size) {
    if (net_engine_ == nullptr || socket_id_ == 0) {
        return false;
    }
    return net_engine_->SendTo(socket_id_, std::move(address), data, size);
}

}  // namespace webrtc_internal
}  // namespace live_stream
