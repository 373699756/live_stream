#include "osd_service.h"

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
        if (name != "osd") {
            return infra::Status::kInvalidParam;
        }
        if (verify && verify(value) != infra::Status::kOk) {
            return infra::Status::kInvalidParam;
        }
        return apply ? apply(value) : infra::Status::kOk;
    }
    infra::Status GetValue(const std::string& name,
                           live_stream::ConfigJson* value) override {
        if (name != "osd" || value == nullptr) {
            return infra::Status::kInvalidParam;
        }
        *value = osd;
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
        if (name != "osd") {
            return infra::Status::kInvalidParam;
        }
        apply = proc;
        return infra::Status::kOk;
    }
    infra::Status RegisterVerify(const std::string& name,
                                 live_stream::ConfigProc proc) override {
        if (name != "osd") {
            return infra::Status::kInvalidParam;
        }
        verify = proc;
        return infra::Status::kOk;
    }

    live_stream::ConfigJson osd = {
        {"enabled", true},
        {"items",
         {{"timestamp", {{"enabled", true}, {"x", 16}, {"y", 16}}},
          {"device_name", {{"enabled", true}, {"x", 16}, {"y", 48}}}}}};
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

    live_stream::OsdService osd;
    if (std::strcmp(live_stream::OsdService::StaticName(), "osd_service") != 0) {
        return 3;
    }
    if (std::strcmp(osd.Name(), "osd_service") != 0) {
        return 4;
    }
    if (osd.Init() != infra::Status::kOk) {
        return 5;
    }
    if (osd.Start() != infra::Status::kBusy) {
        return 6;
    }
    if (osd.BindMedia(channels) != infra::Status::kOk) {
        return 7;
    }
    if (osd.Start() != infra::Status::kOk) {
        return 8;
    }

    live_stream::OsdRegionConfig config;
    config.target = channels.venc;
    const infra::Result<live_stream::OsdRegionId> id = osd.CreateRegion(config);
    if (!id.IsOk()) {
        return 9;
    }
    if (osd.Attach(id.value) != infra::Status::kOk) {
        return 10;
    }
    if (osd.SetVisible(id.value, false) != infra::Status::kOk) {
        return 11;
    }
    if (osd.RegionCount() != 1) {
        return 12;
    }
    osd.Stop();
    if (osd.DestroyRegion(id.value) != infra::Status::kOk) {
        return 13;
    }
    osd.Deinit();

    FakeConfigService config;
    live_stream::OsdServiceOptions options;
    options.config_service = &config;
    live_stream::OsdService configured(options);
    if (configured.Init() != infra::Status::kOk) {
        return 14;
    }
    if (configured.BindMedia(channels) != infra::Status::kOk) {
        return 15;
    }
    if (configured.Start() != infra::Status::kOk) {
        return 16;
    }
    if (configured.RegionCount() != 2U) {
        return 17;
    }
    config.osd["items"]["timestamp"]["x"] = 32;
    if (config.SetValue("osd", config.osd) != infra::Status::kOk) {
        return 18;
    }
    if (configured.GetStats().config_apply_count < 2) {
        return 19;
    }
    return 0;
}
