#include "webrtc_service.h"

#include <cstring>
#include <memory>

namespace {

class DummyMediaBuffer : public IMediaBuffer {
 public:
    uint8_t* MutableData() override { return data_; }
    const uint8_t* Data() const override { return data_; }
    uint32_t Size() const override { return size_; }
    uint32_t Capacity() const override { return sizeof(data_); }
    void SetSize(uint32_t size) override {
        size_ = size > Capacity() ? Capacity() : size;
    }

 private:
    uint8_t data_[8] = {};
    uint32_t size_ = sizeof(data_);
};

int Expect(bool condition) {
    return condition ? 0 : 1;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::WebrtcService::Name(), "webrtc_service") != 0) {
        return 1;
    }

    live_stream::WebrtcServiceOptions invalid_options;
    invalid_options.max_peers = 0;
    live_stream::WebrtcServiceDependencies dependencies;
    std::unique_ptr<live_stream::IWebrtcService> invalid_service =
        live_stream::CreateWebrtcService(invalid_options, dependencies);
    if (Expect(invalid_service->Init() == infra::Status::kInvalidParam)) {
        return 1;
    }

    live_stream::WebrtcServiceOptions options;
    options.max_peers = 4;
    options.session_timeout_ms = 30000;
    options.local_port_base = 16000;
    options.public_ip = "192.0.2.10";
    options.ice_servers.push_back(live_stream::WebrtcIceServer{
        "stun:stun.example.com:3478", "", ""});

    std::unique_ptr<live_stream::IWebrtcService> service =
        live_stream::CreateWebrtcService(options, dependencies);
    if (Expect(service->Init() == infra::Status::kOk) ||
        Expect(service->Start() == infra::Status::kOk) ||
        Expect(std::strcmp(service->BackendName(), "metaRTC") == 0)) {
        return 1;
    }

    live_stream::WebrtcServiceStats initial_stats = service->GetStats();
    if (Expect(initial_stats.enabled) ||
        Expect(!initial_stats.backend_available) ||
        Expect(initial_stats.active_peers == 0) ||
        Expect(initial_stats.max_peers == 4)) {
        return 1;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    if (Expect(service->CreatePeer(create_request).status ==
               infra::Status::kNotSupported)) {
        return 1;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = "missing-peer";
    offer.sdp = "v=0\r\n";
    if (Expect(service->HandleOffer(offer).status == infra::Status::kNotFound)) {
        return 1;
    }

    live_stream::WebrtcIceCandidate candidate;
    candidate.peer_id = "missing-peer";
    candidate.candidate = "candidate:1 1 UDP 1 10.0.0.2 10000 typ host";
    if (Expect(service->AddIceCandidate(candidate) == infra::Status::kNotFound)) {
        return 1;
    }

    EncodedFrame frame;
    frame.stream_id = StreamId::kMain;
    frame.buffer = std::shared_ptr<IMediaBuffer>(new DummyMediaBuffer());
    frame.size = 4;
    service->OnFrame(frame);

    service->Stop();
    if (Expect(service->CreatePeer(create_request).status == infra::Status::kBusy)) {
        return 1;
    }
    service->Deinit();
    return 0;
}
