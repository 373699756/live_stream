#include "webrtc.h"

#include "fake_media_source.h"
#include "net.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

class FakeNetEngine : public live_stream::INetEngine {
public:
    class FakeNetExecutor : public live_stream::INetExecutor {
    public:
        explicit FakeNetExecutor(FakeNetEngine *engine) : engine_(engine) {}

        bool Post(infra::Task task) override {
            if (task) {
                task();
            }
            return true;
        }

        live_stream::NetTimerId RunAfter(uint32_t, infra::Task) override {
            return 1;
        }

        live_stream::NetTimerId RunEvery(uint32_t, infra::Task) override {
            ++engine_->periodic_timer_count;
            return static_cast<live_stream::NetTimerId>(
                engine_->periodic_timer_count);
        }

        bool CancelTimer(live_stream::NetTimerId) override { return true; }
        bool IsCurrentThread() const override { return true; }

    private:
        FakeNetEngine *engine_ = nullptr;
    };

    FakeNetEngine() : executor_(this) {}

    bool Start() override { return true; }
    void Stop() override {}

    live_stream::INetExecutor *DefaultExecutor() override {
        return &executor_;
    }

    live_stream::INetExecutor *PickExecutor() override {
        return &executor_;
    }

    live_stream::TcpServerId ListenTcp(
        live_stream::INetExecutor *,
        const live_stream::TcpListenOptions&,
        const live_stream::TcpCallbacks&) override {
        return 1;
    }

    bool CloseTcp(live_stream::TcpServerId) override { return true; }

    live_stream::UdpSocketId BindUdp(
        live_stream::INetExecutor *,
        const live_stream::UdpBindOptions& options,
        const live_stream::UdpCallbacks&) override {
        last_udp_bind = options.address;
        ++bind_udp_count;
        return static_cast<live_stream::UdpSocketId>(bind_udp_count);
    }

    bool CloseUdp(live_stream::UdpSocketId) override {
        ++close_udp_count;
        return true;
    }

    bool Send(live_stream::ConnectionId, const uint8_t*, size_t) override {
        return true;
    }

    bool Close(live_stream::ConnectionId) override { return true; }
    bool CloseAfterSend(live_stream::ConnectionId) override { return true; }

    bool SendTo(live_stream::UdpSocketId, live_stream::NetAddress,
                const uint8_t*, size_t) override {
        ++send_to_count;
        return true;
    }

    bool SetUdpPeer(live_stream::UdpSocketId, live_stream::NetAddress) override {
        return true;
    }

    bool SendToPeer(live_stream::UdpSocketId, const uint8_t*, size_t) override {
        return true;
    }

    live_stream::NetAddress TcpLocalAddress(
        live_stream::TcpServerId) const override {
        return live_stream::NetAddress{"127.0.0.1", 8000};
    }

    live_stream::NetAddress UdpLocalAddress(
        live_stream::UdpSocketId) const override {
        return live_stream::NetAddress{"127.0.0.1", last_udp_bind.port};
    }

    live_stream::NetAddress UdpPeerAddress(
        live_stream::UdpSocketId) const override {
        return live_stream::NetAddress{"127.0.0.1", 40000};
    }

    uint32_t PendingBytes(live_stream::ConnectionId) const override {
        return 0;
    }

    live_stream::NetStats GetStats() const override {
        return live_stream::NetStats();
    }

    int bind_udp_count = 0;
    int close_udp_count = 0;
    int send_to_count = 0;
    int periodic_timer_count = 0;
    live_stream::NetAddress last_udp_bind;

private:
    FakeNetExecutor executor_;
};

std::string ValidOfferSdp() {
    return
        "v=0\r\n"
        "o=- 1 1 IN IP4 127.0.0.1\r\n"
        "s=test\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0\r\n"
        "a=ice-ufrag:remoteufrag\r\n"
        "a=ice-pwd:remotepassword1234567890\r\n"
        "a=fingerprint:sha-256 "
        "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:"
        "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=mid:0\r\n"
        "a=recvonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1\r\n";
}

}  // namespace

int main() {
    live_stream::test::FakeMediaFrameSource media_source;
    FakeNetEngine net_engine;

    live_stream::WebrtcOptions options;
    options.local_port_base = 16000;
    options.public_ip = "127.0.0.1";

    live_stream::WebrtcDependencies dependencies;
    dependencies.media_source = &media_source;
    dependencies.net_engine = &net_engine;
    dependencies.net_executor = net_engine.DefaultExecutor();

    std::unique_ptr<live_stream::IWebrtc> service =
        live_stream::CreateWebrtc(options, dependencies);
    if (!service || !service->Start()) {
        return 1;
    }
    if (!service->GetStats().enabled || !service->GetStats().signaling_ready) {
        return 2;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    create_request.stream_id = live_stream::StreamId::kSub;
    live_stream::WebrtcPeerInfo peer = service->CreatePeer(create_request);
    if (peer.peer_id.empty() || media_source.key_frame_requests != 1 ||
        media_source.last_key_frame_stream != live_stream::StreamId::kSub ||
        media_source.last_key_frame_reason !=
            live_stream::KeyFrameRequestType::kNewSubscriber) {
        return 3;
    }

    live_stream::WebrtcCreatePeerRequest overflow_request;
    overflow_request.stream_id = live_stream::StreamId::kMain;
    if (!service->CreatePeer(overflow_request).peer_id.empty()) {
        return 4;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = peer.peer_id;
    offer.sdp = ValidOfferSdp();
    live_stream::WebrtcAnswer answer = service->HandleOffer(offer);
    if (answer.sdp.empty() || net_engine.bind_udp_count != 1 ||
        net_engine.last_udp_bind.port != 16000) {
        return 5;
    }

    live_stream::WebrtcIceCandidate candidate;
    candidate.peer_id = peer.peer_id;
    candidate.candidate =
        "candidate:1 1 udp 2130706431 127.0.0.1 40000 typ host";
    if (!service->AddIceCandidate(candidate)) {
        return 6;
    }

    if (service->GetPeer(peer.peer_id).peer_id != peer.peer_id) {
        return 7;
    }

    if (!service->ClosePeer(peer.peer_id) ||
        service->GetStats().active_peers != 0) {
        return 8;
    }

    service->Stop();
    if (net_engine.close_udp_count == 0) {
        return 9;
    }
    return 0;
}
