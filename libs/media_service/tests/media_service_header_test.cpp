#include "media_service.h"

#include <cstring>

namespace {

class TestFrameSink : public live_stream::IFrameSink {
 public:
    const char* Name() const override { return "test_sink"; }
    void OnFrame(const infra::EncodedFrame& frame) override {
        (void)frame;
        ++frames;
    }
    void OnSourceStateChanged(infra::StreamId stream_id,
                              live_stream::StreamState state) override {
        (void)stream_id;
        last_state = state;
        ++state_changes;
    }

    int frames = 0;
    int state_changes = 0;
    live_stream::StreamState last_state = live_stream::StreamState::kClosed;
};

}  // namespace

int main() {
    live_stream::MediaService service;
    TestFrameSink sink;
    live_stream::FrameSubscribeOptions options;

    if (std::strcmp(live_stream::MediaService::StaticName(), "media_service") != 0) {
        return 1;
    }
    if (std::strcmp(service.Name(), "media_service") != 0) {
        return 2;
    }
    if (service.GetChannels().IsOk()) {
        return 3;
    }
    if (service.Init() != infra::Status::kOk) {
        return 4;
    }
    infra::Result<live_stream::MediaCapabilities> capabilities =
        service.GetCapabilities();
    if (!capabilities.IsOk() || capabilities.value.streams.size() < 2 ||
        capabilities.value.streams[0].resolutions.empty() ||
        capabilities.value.streams[0].codecs.empty()) {
        return 14;
    }
    if (service.SubscribeFrames(options, &sink).status != infra::Status::kBusy) {
        return 8;
    }
    if (!service.GetChannels().IsOk()) {
        return 5;
    }
    if (service.Start() != infra::Status::kOk) {
        return 6;
    }
    if (service.SetEncodedFrameCallback(nullptr, nullptr) != infra::Status::kBusy) {
        return 7;
    }
    infra::Result<live_stream::FrameSubscriptionId> subscription =
        service.SubscribeFrames(options, &sink);
    if (!subscription.IsOk()) {
        return 9;
    }
    if (sink.last_state != live_stream::StreamState::kRunning ||
        sink.state_changes != 1) {
        return 10;
    }
    if (service.RequestKeyFrame(infra::StreamId::kMain,
                                live_stream::KeyFrameReason::kNewClient) !=
        infra::Status::kOk) {
        return 11;
    }
    if (service.UnsubscribeFrames(subscription.value) != infra::Status::kOk) {
        return 12;
    }
    if (service.UnsubscribeFrames(subscription.value) != infra::Status::kNotFound) {
        return 13;
    }
    service.Stop();
    service.Stop();
    service.Deinit();
    service.Deinit();
    return 0;
}
