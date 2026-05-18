#include "webrtc_service.h"

#include "media_service.h"
#include "net_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

class FakeMediaService : public live_stream::MediaService {
 public:
    infra::Result<live_stream::FrameSubscriptionId> SubscribeFrames(
        const live_stream::FrameSubscribeOptions& options,
        live_stream::IFrameSink* sink) override {
        if (sink == nullptr) {
            return infra::Result<live_stream::FrameSubscriptionId>::Fail(
                infra::Status::kInvalidParam);
        }
        if (options.stream_id == StreamId::kMain) {
            main_sink = sink;
            return infra::Result<live_stream::FrameSubscriptionId>::Ok(1);
        }
        if (options.stream_id == StreamId::kSub) {
            sub_sink = sink;
            return infra::Result<live_stream::FrameSubscriptionId>::Ok(2);
        }
        return infra::Result<live_stream::FrameSubscriptionId>::Fail(
            infra::Status::kInvalidParam);
    }

    infra::Status UnsubscribeFrames(
        live_stream::FrameSubscriptionId subscription_id) override {
        if (subscription_id == 1) {
            main_sink = nullptr;
            return infra::Status::kOk;
        }
        if (subscription_id == 2) {
            sub_sink = nullptr;
            return infra::Status::kOk;
        }
        return infra::Status::kNotFound;
    }

    infra::Status RequestKeyFrame(StreamId stream_id,
                                  live_stream::KeyFrameReason reason) override {
        last_key_frame_stream = stream_id;
        last_key_frame_reason = reason;
        ++key_frame_requests;
        return infra::Status::kOk;
    }

    live_stream::IFrameSink* main_sink = nullptr;
    live_stream::IFrameSink* sub_sink = nullptr;
    StreamId last_key_frame_stream = StreamId::kSnapshot;
    live_stream::KeyFrameReason last_key_frame_reason =
        live_stream::KeyFrameReason::kRecovery;
    int key_frame_requests = 0;
};

class FakeNetEngine : public live_stream::NetEngine {
 public:
    infra::Status Start() override {
        ++start_count;
        return infra::Status::kOk;
    }

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
        const live_stream::UdpBindOptions& options,
        const live_stream::UdpCallbacks&) override {
        last_udp_bind = options.address;
        ++bind_udp_count;
        return infra::Result<live_stream::UdpSocketId>::Ok(7);
    }

    infra::Status CloseUdp(live_stream::UdpSocketId) override {
        ++close_udp_count;
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
        return infra::Result<live_stream::NetAddress>::Ok(last_udp_bind);
    }

    uint32_t PendingBytes(live_stream::ConnectionId) const override {
        return 0;
    }

    live_stream::NetStats GetStats() const override {
        return live_stream::NetStats();
    }

    int start_count = 0;
    int bind_udp_count = 0;
    int close_udp_count = 0;
    live_stream::NetAddress last_udp_bind;
};

}  // namespace

int main() {
    FakeMediaService media;
    FakeNetEngine net_engine;

    live_stream::WebrtcServiceOptions options;
    options.local_port_base = 16000;

    live_stream::WebrtcServiceDependencies dependencies;
    dependencies.media_service = &media;
    dependencies.net_engine = &net_engine;
    dependencies.use_fake_engine = true;

    std::unique_ptr<live_stream::IWebrtcService> service =
        live_stream::CreateWebrtcService(options, dependencies);
    if (!service || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 1;
    }
    if (media.main_sink == nullptr || media.sub_sink == nullptr) {
        return 2;
    }
    if (net_engine.bind_udp_count != 1 || net_engine.start_count != 1 ||
        net_engine.last_udp_bind.port != 16000) {
        return 3;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    create_request.stream_id = StreamId::kSub;
    auto peer = service->CreatePeer(create_request);
    if (!peer.IsOk() || media.key_frame_requests != 1 ||
        media.last_key_frame_stream != StreamId::kSub ||
        media.last_key_frame_reason != live_stream::KeyFrameReason::kNewClient) {
        return 4;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = peer.value.peer_id;
    offer.sdp = "v=0\r\n";
    if (!service->HandleOffer(offer).IsOk() || media.key_frame_requests != 2) {
        return 5;
    }

    service->Stop();
    if (media.main_sink != nullptr || media.sub_sink != nullptr ||
        net_engine.close_udp_count != 1) {
        return 6;
    }
    service->Deinit();
    return 0;
}
