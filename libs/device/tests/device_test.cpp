#include "device.h"

#include "config.h"

#include <map>
#include <memory>
#include <string>

namespace {

class TestFrameSink : public live_stream::FrameSink {
public:
    bool PushFrame(const live_stream::EncodedFrame& frame) override {
        (void)frame;
        ++frames;
        return true;
    }

    int frames = 0;
};

class FakeConfig : public live_stream::IConfig {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::ConfigStatus Set(const std::string& name,
                                  const live_stream::ConfigJson& now,
                                  live_stream::ConfigIssue* issue) override {
        auto iter = scopes.find(name);
        if (iter != scopes.end() && iter->second.verify) {
            const live_stream::ConfigStatus status =
                iter->second.verify(now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        if (iter != scopes.end() && iter->second.apply) {
            const live_stream::ConfigJson prev = Get(name);
            const live_stream::ConfigStatus status =
                iter->second.apply(prev, now, issue);
            if (status != live_stream::ConfigStatus::kOk) {
                return status;
            }
        }
        values[name] = now;
        return live_stream::ConfigStatus::kOk;
    }

    live_stream::ConfigJson Get(const std::string& name) override {
        auto iter = values.find(name);
        return iter != values.end() ? iter->second
                                    : live_stream::ConfigJson::object();
    }

    live_stream::ConfigJson Default(const std::string& name) override {
        (void)name;
        return live_stream::ConfigJson::object();
    }

    live_stream::ConfigStatus Reset(
        const std::string& name, live_stream::ConfigIssue*) override {
        values[name] = Default(name);
        return live_stream::ConfigStatus::kOk;
    }

    live_stream::ConfigStatus ResetAll(
        live_stream::ConfigIssue*) override {
        return live_stream::ConfigStatus::kOk;
    }

    bool AddScope(const std::string& name,
                  const live_stream::ConfigScope& scope) override {
        scopes[name] = scope;
        ++attach_count;
        return true;
    }

    bool RemoveScope(const std::string& name) override {
        return scopes.erase(name) != 0;
    }

    std::map<std::string, live_stream::ConfigJson> values;
    std::map<std::string, live_stream::ConfigScope> scopes;
    int attach_count = 0;
};

live_stream::ConfigJson BuildVideoConfig(uint32_t bitrate_kbps) {
    return {{"streams",
             {{"main",
               {{"codec", "h265"},
                {"resolution", "1920x1080"},
                {"fps", 25},
                {"bitrate_kbps", bitrate_kbps},
                {"rate_control", "cbr"},
                {"gop", 50},
                {"gop_mode", "smart_p"}}}}}};
}

}  // namespace

int main() {
    std::unique_ptr<live_stream::DeviceMedia> service =
        live_stream::CreateDeviceMedia();
    if (!service) {
        return 1;
    }
    if (service->IsStarted() || service->GetChannels().venc.channel != 0) {
        return 3;
    }
    const live_stream::MediaCapabilities capabilities =
        service->GetCapabilities();
    if (capabilities.streams.size() < 2 ||
        capabilities.streams[0].resolutions.empty() ||
        capabilities.streams[0].codecs.size() < 4 ||
        capabilities.image.basic.size() < 5 ||
        capabilities.image.exposure_options.empty()) {
        return 4;
    }

    TestFrameSink sink;
    if (!service->SetFrameSink(&sink)) {
        return 5;
    }
    if (!service->Start() || !service->IsStarted()) {
        return 6;
    }
    if (!service->RequestKeyframe(
            live_stream::StreamId::kMain,
            live_stream::KeyframeRequestSource::kNewClient)) {
        return 7;
    }
    service->Stop();
    service->Stop();

    FakeConfig config;
    config.values["video"] = BuildVideoConfig(4096);
    live_stream::DeviceMediaOptions service_options;
    service_options.config = &config;
    std::unique_ptr<live_stream::DeviceMedia> configured =
        live_stream::CreateDeviceMedia(service_options);
    if (!configured || !configured->Start()) {
        return 8;
    }
    if (config.attach_count != 2 ||
        config.scopes.find("video") == config.scopes.end() ||
        config.scopes.find("image") == config.scopes.end()) {
        return 9;
    }
    config.values["video"] = BuildVideoConfig(2048);
    if (config.Set("video", config.values["video"], nullptr) !=
        live_stream::ConfigStatus::kOk) {
        return 10;
    }
    configured->Stop();
    return 0;
}
