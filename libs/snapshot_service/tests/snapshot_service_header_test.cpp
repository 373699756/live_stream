#include "snapshot_service.h"

#include "config_service.h"

#include <cstring>

namespace {

class FakeConfigService : public live_stream::IConfigService {
 public:
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_config"; }

    infra::Status SetValue(const std::string& name,
                           const live_stream::ConfigJson& value) override {
        if (name != "snapshot") {
            return infra::Status::kInvalidParam;
        }
        if (verify && verify(value) != infra::Status::kOk) {
            return infra::Status::kInvalidParam;
        }
        return apply ? apply(value) : infra::Status::kOk;
    }
    infra::Status GetValue(const std::string& name,
                           live_stream::ConfigJson* value) override {
        if (name != "snapshot" || value == nullptr) {
            return infra::Status::kInvalidParam;
        }
        *value = snapshot;
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
        if (name != "snapshot") {
            return infra::Status::kInvalidParam;
        }
        apply = proc;
        return infra::Status::kOk;
    }
    infra::Status RegisterVerify(const std::string& name,
                                 live_stream::ConfigProc proc) override {
        if (name != "snapshot") {
            return infra::Status::kInvalidParam;
        }
        verify = proc;
        return infra::Status::kOk;
    }

    live_stream::ConfigJson snapshot = {
        {"enabled", true},
        {"main_path", "/api/snapshot/main.jpg"},
        {"sub_path", "/api/snapshot/sub.jpg"},
        {"jpeg_quality", 85},
        {"timeout_ms", 2000}};
    live_stream::ConfigProc verify;
    live_stream::ConfigProc apply;
};

}  // namespace

int main() {
    live_stream::MediaChannels channels;
    channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
    channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
    channels.video_pipe = 0;
    channels.snap_pipe = 2;

    live_stream::SnapshotService snapshot;
    if (std::strcmp(live_stream::SnapshotService::StaticName(), "snapshot_service") != 0) {
        return 3;
    }
    if (std::strcmp(snapshot.Name(), "snapshot_service") != 0) {
        return 4;
    }
    if (snapshot.Init() != infra::Status::kOk) {
        return 5;
    }
    if (snapshot.Start() != infra::Status::kBusy) {
        return 6;
    }
    if (snapshot.BindMedia(channels) != infra::Status::kOk) {
        return 7;
    }
    if (snapshot.Start() != infra::Status::kOk) {
        return 8;
    }

    live_stream::CaptureRequest request;
    const infra::Result<live_stream::SnapshotFrame> frame = snapshot.Capture(request);
    if (!frame.IsOk()) {
        return 9;
    }
    if (frame.value.width != 1920 || frame.value.height != 1080) {
        return 10;
    }
    if (!frame.value.buffer || frame.value.size == 0 ||
        !infra::IsValidBufferSlice(frame.value.PayloadSlice())) {
        return 12;
    }
    if (snapshot.IsCapturing()) {
        return 11;
    }
    snapshot.Stop();
    snapshot.Deinit();

    FakeConfigService config;
    live_stream::SnapshotServiceOptions options;
    options.config_service = &config;
    live_stream::SnapshotService configured(options);
    if (configured.Init() != infra::Status::kOk) {
        return 13;
    }
    if (configured.BindMedia(channels) != infra::Status::kOk) {
        return 14;
    }
    if (configured.Start() != infra::Status::kOk) {
        return 15;
    }
    live_stream::SnapshotServiceStats stats = configured.GetStats();
    if (stats.jpeg_quality != 85U || stats.timeout_ms != 2000U) {
        return 16;
    }
    config.snapshot["enabled"] = false;
    if (config.SetValue("snapshot", config.snapshot) != infra::Status::kOk) {
        return 17;
    }
    if (configured.Capture(request).status != infra::Status::kBusy) {
        return 18;
    }
    return 0;
}
