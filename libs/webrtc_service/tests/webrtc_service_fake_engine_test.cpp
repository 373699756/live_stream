#include "webrtc_service.h"

#include "infra/time.h"

#include <cstring>
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
        live_stream::IStreamFlvSink* sink) override {
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
    live_stream::FrameAttachId AttachFrameSink(
        const live_stream::FrameAttachOptions& options,
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
    bool DetachFrameSink(live_stream::FrameAttachId sink_id) override {
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
        (void)stream_id;
        (void)reason;
        return true;
    }
    live_stream::StreamHubServiceStats GetStats() const override {
        live_stream::StreamHubServiceStats stats;
        stats.enabled = true;
        return stats;
    }

    live_stream::IFrameSink* main_sink = nullptr;
    live_stream::IFrameSink* sub_sink = nullptr;
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

    FakeStreamHub stream_hub;
    live_stream::WebrtcServiceDependencies dependencies;
    dependencies.stream_hub = &stream_hub;
    dependencies.use_fake_engine = true;

    std::unique_ptr<live_stream::IWebrtcService> service =
        live_stream::CreateWebrtcService(options, dependencies);
    if (!service || !service->Start()) {
        return 1;
    }
    if (std::strcmp(service->BackendName(), "fake_webrtc") != 0) {
        return 2;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    create_request.stream_id = live_stream::StreamId::kMain;
    live_stream::WebrtcPeerInfo peer = service->CreatePeer(create_request);
    if (peer.peer_id.empty()) {
        return 3;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = peer.peer_id;
    offer.sdp = "v=0\r\n";
    live_stream::WebrtcAnswer answer = service->HandleOffer(offer);
    if (answer.sdp.find("fake-webrtc-answer") == std::string::npos) {
        return 4;
    }

    live_stream::WebrtcIceCandidate candidate;
    candidate.peer_id = peer.peer_id;
    candidate.candidate = "candidate:1 1 UDP 1 10.0.0.2 10000 typ host";
    if (!service->AddIceCandidate(candidate)) {
        return 5;
    }

    live_stream::EncodedFrame frame;
    frame.stream_id = live_stream::StreamId::kMain;
    frame.buffer = live_stream::VideoBufferAlloc(16);
    frame.size = 8;
    (void)live_stream::VideoBufferSetSize(frame.buffer, 16);
    live_stream::FramePayload payload;
    payload.encoded_frame = frame;
    payload.has_nal_units = true;
    service->OnFrame(payload);
    if (!WaitForSentFrames(service.get(), 1)) {
        return 6;
    }

    service->Stop();
    return 0;
}
