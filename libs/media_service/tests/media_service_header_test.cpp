#include "media_service.h"

#include "config_service.h"

#include <cstring>

namespace {

class TestFrameSink : public live_stream::IFrameSink {
public:
    const char* Name() const override { return "test_sink"; }
    void OnFrame(const EncodedFrame& frame) override {
        (void)frame;
        ++frames;
    }
    void OnSourceStateChanged(StreamId stream_id,
                              live_stream::StreamState state) override {
        (void)stream_id;
        last_state = state;
        ++state_changes;
    }

    int frames = 0;
    int state_changes = 0;
    live_stream::StreamState last_state = live_stream::StreamState::kClosed;
};

class FakeConfigService : public live_stream::IConfigService {
public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_config"; }

    infra::Status SetValue(const std::string& name,
                           const live_stream::ConfigJson& value) override {
        if (verify && verify(value) != infra::Status::kOk) {
            return infra::Status::kInvalidParam;
        }
        if (apply) {
            return apply(value);
        }
        (void)name;
        return infra::Status::kOk;
    }

    infra::Status GetValue(const std::string& name,
                           live_stream::ConfigJson* value) override {
        if (name != "video" || value == nullptr) {
            return infra::Status::kInvalidParam;
        }
        *value = video;
        return infra::Status::kOk;
    }

    infra::Status GetDefault(const std::string&,
                             live_stream::ConfigJson*) override {
        return infra::Status::kNotFound;
    }
    infra::Status RestoreDefaults() override { return infra::Status::kOk; }
    infra::Status SaveFile() override { return infra::Status::kOk; }
    infra::Status RegisterApply(const std::string& name,
                                live_stream::ConfigProc proc) override {
        if (name != "video") {
            return infra::Status::kInvalidParam;
        }
        apply = proc;
        return infra::Status::kOk;
    }
    infra::Status RegisterVerify(const std::string& name,
                                 live_stream::ConfigProc proc) override {
        if (name != "video") {
            return infra::Status::kInvalidParam;
        }
        verify = proc;
        return infra::Status::kOk;
    }

    live_stream::ConfigJson video = {
        {"streams",
         {{"main",
           {{"codec", "h265"},
            {"resolution", "1920x1080"},
            {"fps", 25},
            {"bitrate_kbps", 4096},
            {"rate_control", "cbr"},
            {"gop", 50},
            {"gop_mode", "smart_p"}}}}}};
    live_stream::ConfigProc verify;
    live_stream::ConfigProc apply;
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
        capabilities.value.streams[0].codecs.size() < 4 ||
        capabilities.value.image.basic.size() < 5 ||
        capabilities.value.image.exposure_options.empty()) {
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
    if (service.RequestKeyFrame(StreamId::kMain,
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

    FakeConfigService config;
    live_stream::MediaServiceOptions service_options;
    service_options.config_service = &config;
    live_stream::MediaService configured(service_options);
    if (configured.Init() != infra::Status::kOk) {
        return 15;
    }
    live_stream::MediaServiceStats stats = configured.GetStats();
    if (stats.config_apply_count != 1) {
        return 16;
    }
    config.video["streams"]["main"]["bitrate_kbps"] = 2048;
    if (config.SetValue("video", config.video) != infra::Status::kOk) {
        return 17;
    }
    stats = configured.GetStats();
    if (stats.config_apply_count != 2) {
        return 18;
    }
    return 0;
}
