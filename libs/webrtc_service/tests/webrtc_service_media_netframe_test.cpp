#include "webrtc_service.h"

#include "net_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

class FakeStreamHub : public live_stream::IStreamHubService {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsHlsSupported(live_stream::StreamId stream_id) const override {
        (void)stream_id;
        return false;
    }
    bool IsFlvSupported(live_stream::StreamId stream_id) const override {
        (void)stream_id;
        return false;
    }
    bool IsStreamAvailable(live_stream::StreamId stream_id) const override {
        return stream_id == live_stream::StreamId::kMain ||
               stream_id == live_stream::StreamId::kSub;
    }
    live_stream::VideoCodec GetStreamCodec(
        live_stream::StreamId stream_id) const override {
        (void)stream_id;
        return live_stream::VideoCodec::kH264;
    }
    live_stream::StreamHlsPlaylist GetHlsPlaylist(
        live_stream::StreamId stream_id) const override {
        (void)stream_id;
        return live_stream::StreamHlsPlaylist{};
    }
    live_stream::StreamSegment GetHlsSegment(
        live_stream::StreamId stream_id, uint64_t sequence) const override {
        (void)stream_id;
        (void)sequence;
        return live_stream::StreamSegment{};
    }
    live_stream::StreamFlvStartData GetFlvStartData(
        live_stream::StreamId stream_id) const override {
        (void)stream_id;
        return live_stream::StreamFlvStartData{};
    }
    live_stream::StreamBrowserStatus GetBrowserStatus(
        live_stream::StreamId stream_id) const override {
        (void)stream_id;
        return live_stream::StreamBrowserStatus{};
    }
    live_stream::StreamFlvClientId AttachFlvClient(
        live_stream::StreamId stream_id, uint64_t config_generation,
        bool wait_for_keyframe,
        const std::shared_ptr<live_stream::IStreamFlvSink>& sink) override {
        (void)stream_id;
        (void)config_generation;
        (void)wait_for_keyframe;
        (void)sink;
        return 0;
    }
    bool DetachFlvClient(live_stream::StreamFlvClientId client_id) override {
        (void)client_id;
        return false;
    }
    live_stream::StreamFrameSinkId AttachFrameSink(
        const live_stream::StreamFrameSinkOptions& options,
        live_stream::IFrameSink* sink) override {
        if (sink == nullptr) {
            return 0;
        }
        if (options.stream_id == live_stream::StreamId::kMain) {
            main_sink = sink;
            return 1;
        }
        if (options.stream_id == live_stream::StreamId::kSub) {
            sub_sink = sink;
            return 2;
        }
        return 0;
    }
    bool DetachFrameSink(live_stream::StreamFrameSinkId sink_id) override {
        if (sink_id == 1) {
            main_sink = nullptr;
            return true;
        }
        if (sink_id == 2) {
            sub_sink = nullptr;
            return true;
        }
        return false;
    }
    bool RequestKeyFrame(live_stream::StreamId stream_id,
                         live_stream::KeyFrameReason reason) override {
        last_key_frame_stream = stream_id;
        last_key_frame_reason = reason;
        ++key_frame_requests;
        return true;
    }
    live_stream::StreamHubServiceStats GetStats() const override {
        live_stream::StreamHubServiceStats stats;
        stats.enabled = true;
        return stats;
    }

    live_stream::IFrameSink* main_sink = nullptr;
    live_stream::IFrameSink* sub_sink = nullptr;
    live_stream::StreamId last_key_frame_stream =
        live_stream::StreamId::kSnapshot;
    live_stream::KeyFrameReason last_key_frame_reason =
        live_stream::KeyFrameReason::kRecovery;
    int key_frame_requests = 0;
};

class FakeNetEngine : public live_stream::NetEngine {
public:
    bool Start() override {
        ++start_count;
        return true;
    }

    void Stop() override {}

    live_stream::TcpServerId ListenTcp(
        const live_stream::TcpListenOptions&,
        const live_stream::TcpCallbacks&) override {
        return 1;
    }

    bool CloseTcp(live_stream::TcpServerId) override {
        return true;
    }

    live_stream::UdpSocketId BindUdp(
        const live_stream::UdpBindOptions& options,
        const live_stream::UdpCallbacks&) override {
        last_udp_bind = options.address;
        ++bind_udp_count;
        return 7;
    }

    bool CloseUdp(live_stream::UdpSocketId) override {
        ++close_udp_count;
        return true;
    }

    bool Send(live_stream::ConnectionId, const uint8_t*, size_t) override {
        return true;
    }

    bool Close(live_stream::ConnectionId) override {
        return true;
    }

    bool CloseAfterSend(live_stream::ConnectionId) override {
        return true;
    }

    bool SendTo(live_stream::UdpSocketId, live_stream::NetAddress,
                const uint8_t*, size_t) override {
        return true;
    }

    live_stream::NetTimerId RunOnIoAfter(uint32_t, infra::Task) override {
        return 1;
    }

    bool CancelIoTimer(live_stream::NetTimerId) override {
        return true;
    }

    live_stream::NetAddress TcpLocalAddress(
        live_stream::TcpServerId) const override {
        return live_stream::NetAddress{"127.0.0.1", 8000};
    }

    live_stream::NetAddress UdpLocalAddress(
        live_stream::UdpSocketId) const override {
        return last_udp_bind;
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
    FakeStreamHub stream_hub;
    FakeNetEngine net_engine;

    live_stream::WebrtcServiceOptions options;
    options.local_port_base = 16000;

    live_stream::WebrtcServiceDependencies dependencies;
    dependencies.stream_hub = &stream_hub;
    dependencies.net_engine = &net_engine;
    dependencies.use_fake_engine = true;

    std::unique_ptr<live_stream::IWebrtcService> service =
        live_stream::CreateWebrtcService(options, dependencies);
    if (!service || !service->Start()) {
        return 1;
    }
    if (stream_hub.main_sink == nullptr || stream_hub.sub_sink == nullptr) {
        return 2;
    }
    if (net_engine.bind_udp_count != 1 || net_engine.start_count != 1 ||
        net_engine.last_udp_bind.port != 16000) {
        return 3;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    create_request.stream_id = live_stream::StreamId::kSub;
    live_stream::WebrtcPeerInfo peer = service->CreatePeer(create_request);
    if (peer.peer_id.empty() || stream_hub.key_frame_requests != 1 ||
        stream_hub.last_key_frame_stream != live_stream::StreamId::kSub ||
        stream_hub.last_key_frame_reason !=
            live_stream::KeyFrameReason::kNewClient) {
        return 4;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = peer.peer_id;
    offer.sdp = "v=0\r\n";
    live_stream::WebrtcAnswer answer = service->HandleOffer(offer);
    if (answer.sdp.empty() || stream_hub.key_frame_requests != 2) {
        return 5;
    }

    service->Stop();
    if (stream_hub.main_sink != nullptr || stream_hub.sub_sink != nullptr ||
        net_engine.close_udp_count != 1) {
        return 6;
    }
    return 0;
}
