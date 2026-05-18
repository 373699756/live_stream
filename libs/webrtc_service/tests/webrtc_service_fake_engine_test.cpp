#include "webrtc_service.h"

#include "infra/time.h"

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
    uint8_t data_[16] = {};
    uint32_t size_ = sizeof(data_);
};

bool WaitForSentFrames(live_stream::IWebrtcService* service,
                       uint64_t expected) {
    for (int i = 0; i < 20; ++i) {
        if (service->GetStats().sent_frames >= expected) {
            return true;
        }
        infra::Time::SleepMillis(10);
    }
    return false;
}

}  // namespace

int main() {
    live_stream::WebrtcServiceOptions options;
    options.send_worker_count = 2;

    live_stream::WebrtcServiceDependencies dependencies;
    dependencies.use_fake_engine = true;

    std::unique_ptr<live_stream::IWebrtcService> service =
        live_stream::CreateWebrtcService(options, dependencies);
    if (!service || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 1;
    }
    if (std::strcmp(service->BackendName(), "fake_webrtc") != 0) {
        return 2;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    create_request.stream_id = StreamId::kMain;
    auto peer = service->CreatePeer(create_request);
    if (!peer.IsOk()) {
        return 3;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = peer.value.peer_id;
    offer.sdp = "v=0\r\n";
    auto answer = service->HandleOffer(offer);
    if (!answer.IsOk() ||
        answer.value.sdp.find("fake-webrtc-answer") == std::string::npos) {
        return 4;
    }

    live_stream::WebrtcIceCandidate candidate;
    candidate.peer_id = peer.value.peer_id;
    candidate.candidate = "candidate:1 1 UDP 1 10.0.0.2 10000 typ host";
    if (service->AddIceCandidate(candidate) != infra::Status::kOk) {
        return 5;
    }

    EncodedFrame frame;
    frame.stream_id = StreamId::kMain;
    frame.buffer = std::shared_ptr<IMediaBuffer>(new DummyMediaBuffer());
    frame.size = 8;
    service->OnFrame(frame);
    if (!WaitForSentFrames(service.get(), 1)) {
        return 6;
    }

    service->Stop();
    service->Deinit();
    return 0;
}
