#include "region_service.h"

#include "config_service.h"

#include <cstring>

namespace {

class FakeConfigService : public live_stream::IConfigService {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    bool SetValue(const std::string& name,
                  const live_stream::ConfigJson& value) override {
        if (name != "overlay") {
            return false;
        }
        if (attachment.validate && !attachment.validate(value).ok) {
            return false;
        }
        if (attachment.apply && !attachment.apply(value).ok) {
            return false;
        }
        overlay = value;
        return true;
    }
    live_stream::ConfigJson GetValue(const std::string& name) override {
        if (name != "overlay") {
            return live_stream::ConfigJson{};
        }
        return overlay;
    }
    bool SetDefault(const std::string& name) override {
        if (name != "overlay") {
            return false;
        }
        overlay = default_overlay;
        return true;
    }
    live_stream::ConfigJson GetDefault(const std::string& name) override {
        if (name != "overlay") {
            return live_stream::ConfigJson{};
        }
        return default_overlay;
    }
    bool RestoreDefaults() override {
        overlay = default_overlay;
        return true;
    }
    bool AttachConfig(const std::string& name,
                      const live_stream::ConfigAttachment& next) override {
        if (name != "overlay") {
            return false;
        }
        attachment = next;
        return true;
    }
    bool DetachConfig(const std::string& name) override {
        if (name != "overlay") {
            return false;
        }
        attachment = live_stream::ConfigAttachment{};
        return true;
    }

    live_stream::ConfigJson default_overlay = {
        {"enabled", true},
        {"items",
         {{"timestamp",
           {{"enabled", true},
            {"format", "%Y-%m-%d %H:%M:%S"},
            {"x", 16},
            {"y", 16}}},
          {"device_name",
           {{"enabled", true},
            {"text", "IPC Camera"},
            {"x", 16},
            {"y", 48}}}}},
        {"font_size", 24},
        {"font_color", "#FFFFFF"},
        {"background", true},
        {"privacy_masks",
         {{"main",
           {{{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 160},
             {"height", 120}, {"color", "#000000"}},
            {{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 160},
             {"height", 120}, {"color", "#000000"}},
            {{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 160},
             {"height", 120}, {"color", "#000000"}},
            {{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 160},
             {"height", 120}, {"color", "#000000"}}}},
          {"sub",
           {{{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 80},
             {"height", 60}, {"color", "#000000"}},
            {{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 80},
             {"height", 60}, {"color", "#000000"}},
            {{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 80},
             {"height", 60}, {"color", "#000000"}},
            {{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 80},
             {"height", 60}, {"color", "#000000"}}}}}}};
    live_stream::ConfigJson overlay = default_overlay;
    live_stream::ConfigAttachment attachment;
};

}  // namespace

int main() {
    live_stream::MediaChannels channels;
    channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
    channels.sub_vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 1};
    channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
    channels.sub_venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 1};
    channels.video_pipe = 0;
    channels.snap_pipe = 2;
    channels.main_size = live_stream::VideoSize{1920, 1080};
    channels.sub_size = live_stream::VideoSize{640, 360};

    live_stream::RegionService overlay;
    if (std::strcmp(live_stream::RegionService::StaticName(), "region_service") != 0) {
        return 3;
    }
    if (overlay.Start()) {
        return 6;
    }
    if (!overlay.BindMedia(channels)) {
        return 7;
    }
    if (!overlay.Start()) {
        return 8;
    }

    live_stream::RegionConfig config;
    config.target = channels.venc;
    const live_stream::RegionId id = overlay.CreateRegion(config);
    if (id.value == 0) {
        return 9;
    }
    if (!overlay.Attach(id)) {
        return 10;
    }
    if (!overlay.SetVisible(id, false)) {
        return 11;
    }
    if (overlay.RegionCount() != 1) {
        return 12;
    }
    overlay.Stop();
    if (!overlay.DestroyRegion(id)) {
        return 13;
    }

    FakeConfigService config;
    live_stream::RegionServiceOptions options;
    options.config_service = &config;
    options.media_channels = channels;
    live_stream::RegionService configured(options);
    if (!configured.Start()) {
        return 16;
    }
    if (configured.RegionCount() != 4U) {
        return 17;
    }
    config.overlay["items"]["timestamp"]["x"] = 32;
    if (!config.SetValue("overlay", config.overlay)) {
        return 18;
    }
    if (configured.GetStats().config_apply_count < 2) {
        return 19;
    }
    return 0;
}
