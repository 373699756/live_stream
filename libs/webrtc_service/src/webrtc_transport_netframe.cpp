#include "webrtc_transport_netframe.h"

#include <utility>

namespace live_stream {
namespace webrtc_internal {

namespace {

void OnUdp(void*,
           UdpSocketId,
           NetAddress,
           const uint8_t*,
           size_t) {}

}  // namespace

NetframeWebrtcTransport::NetframeWebrtcTransport(NetEngine* net_engine)
    : net_engine_(net_engine) {}

NetframeWebrtcTransport::~NetframeWebrtcTransport() { Stop(); }

infra::Status NetframeWebrtcTransport::Start(const std::string& listen_ip,
                                             uint16_t port) {
    if (socket_id_ != 0) {
        return infra::Status::kOk;
    }
    if (net_engine_ == nullptr) {
        return infra::Status::kNotSupported;
    }
    UdpBindOptions options;
    options.address.ip = listen_ip.empty() ? "0.0.0.0" : listen_ip;
    options.address.port = port;
    UdpCallbacks callbacks;
    callbacks.on_read = OnUdp;
    auto socket = net_engine_->BindUdp(options, callbacks);
    if (!socket.IsOk()) {
        return socket.status;
    }
    socket_id_ = socket.value;
    const infra::Status start_error = net_engine_->Start();
    if (start_error != infra::Status::kOk) {
        Stop();
    }
    return start_error;
}

void NetframeWebrtcTransport::Stop() {
    if (net_engine_ != nullptr && socket_id_ != 0) {
        (void)net_engine_->CloseUdp(socket_id_);
    }
    socket_id_ = 0;
}

infra::Status NetframeWebrtcTransport::SendTo(NetAddress address,
                                              const uint8_t* data,
                                              size_t size) {
    if (net_engine_ == nullptr || socket_id_ == 0) {
        return infra::Status::kNotSupported;
    }
    return net_engine_->SendTo(socket_id_, std::move(address), data, size);
}

}  // namespace webrtc_internal
}  // namespace live_stream
