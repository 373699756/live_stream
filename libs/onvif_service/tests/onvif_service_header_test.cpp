#include "onvif_service.h"

#include "netframe_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class FakeNetEngine : public live_stream::NetEngine {
 public:
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}

    infra::Result<live_stream::TcpServerId> ListenTcp(
        const live_stream::TcpListenOptions&,
        const live_stream::TcpCallbacks&) override {
        return infra::Result<live_stream::TcpServerId>::Ok(1);
    }

    infra::Status CloseTcp(live_stream::TcpServerId) override {
        return infra::Status::kOk;
    }

    infra::Result<live_stream::UdpSocketId> BindUdp(
        const live_stream::UdpBindOptions&,
        const live_stream::UdpCallbacks&) override {
        return infra::Result<live_stream::UdpSocketId>::Ok(1);
    }

    infra::Status CloseUdp(live_stream::UdpSocketId) override {
        return infra::Status::kOk;
    }

    infra::Status Send(live_stream::ConnectionId,
                       const uint8_t*,
                       size_t) override {
        return infra::Status::kOk;
    }

    infra::Status Close(live_stream::ConnectionId) override {
        return infra::Status::kOk;
    }

    infra::Status CloseAfterSend(live_stream::ConnectionId) override {
        return infra::Status::kOk;
    }

    infra::Status SendTo(live_stream::UdpSocketId,
                         live_stream::NetAddress,
                         const uint8_t*,
                         size_t) override {
        return infra::Status::kOk;
    }

    infra::Result<live_stream::NetTimerId> RunOnIoAfter(uint32_t,
                                                        infra::Task) override {
        return infra::Result<live_stream::NetTimerId>::Ok(1);
    }

    infra::Status CancelIoTimer(live_stream::NetTimerId) override {
        return infra::Status::kOk;
    }

    infra::Result<live_stream::NetAddress> TcpLocalAddress(
        live_stream::TcpServerId) const override {
        return infra::Result<live_stream::NetAddress>::Ok(
            live_stream::NetAddress{"127.0.0.1", 8000});
    }

    infra::Result<live_stream::NetAddress> UdpLocalAddress(
        live_stream::UdpSocketId) const override {
        return infra::Result<live_stream::NetAddress>::Ok(
            live_stream::NetAddress{"127.0.0.1", 3702});
    }

    uint32_t PendingBytes(live_stream::ConnectionId) const override {
        return 0;
    }

    live_stream::NetStats GetStats() const override {
        return live_stream::NetStats();
    }
};

int main() {
    FakeNetEngine net_engine;
    live_stream::OnvifServiceOptions options;
    live_stream::OnvifServiceDependencies deps;
    deps.net_engine = &net_engine;

    std::unique_ptr<live_stream::IOnvifService> service =
        live_stream::CreateOnvifService(options, deps);
    if (!service) {
        return 1;
    }
    if (service->Init() != infra::Status::kOk) {
        return 2;
    }
    if (service->Start() != infra::Status::kOk) {
        return 3;
    }
    service->Stop();
    service->Deinit();
    return 0;
}
