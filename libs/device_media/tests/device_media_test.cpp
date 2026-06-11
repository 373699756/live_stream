#include "device_media.h"

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

    bool SetValue(const std::string& name,
                  const live_stream::ConfigJson& value) override {
        auto iter = attachments.find(name);
        if (iter != attachments.end() && iter->second.validate) {
            const live_stream::ConfigResult result =
                iter->second.validate(value);
            if (!result.ok) {
                return false;
            }
        }
        if (iter != attachments.end() && iter->second.apply) {
            const live_stream::ConfigResult result = iter->second.apply(value);
            if (!result.ok) {
                return false;
            }
        }
        values[name] = value;
        return true;
    }

    live_stream::ConfigJson GetValue(const std::string& name) override {
        auto iter = values.find(name);
        return iter != values.end() ? iter->second
                                    : live_stream::ConfigJson::object();
    }

    live_stream::ConfigJson GetDefault(const std::string& name) override {
        (void)name;
        return live_stream::ConfigJson::object();
    }

    bool SetDefault(const std::string& name) override {
        values[name] = GetDefault(name);
        return true;
    }

    bool RestoreDefaults() override { return true; }

    bool AttachConfig(
        const std::string& name,
        const live_stream::ConfigAttachment& attachment) override {
        attachments[name] = attachment;
        ++attach_count;
        return true;
    }

    bool DetachConfig(const std::string& name) override {
        return attachments.erase(name) != 0;
    }

    std::map<std::string, live_stream::ConfigJson> values;
    std::map<std::string, live_stream::ConfigAttachment> attachments;
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
    std::unique_ptr<live_stream::IDeviceMedia> service =
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
    if (!service->RequestKeyFrame(
            live_stream::StreamId::kMain,
            live_stream::KeyFrameRequestType::kNewSubscriber)) {
        return 7;
    }
    service->Stop();
    service->Stop();

    FakeConfig config;
    config.values["video"] = BuildVideoConfig(4096);
    live_stream::DeviceMediaOptions service_options;
    service_options.config = &config;
    std::unique_ptr<live_stream::IDeviceMedia> configured =
        live_stream::CreateDeviceMedia(service_options);
    if (!configured || !configured->Start()) {
        return 8;
    }
    if (config.attach_count != 2 ||
        config.attachments.find("video") == config.attachments.end() ||
        config.attachments.find("image") == config.attachments.end()) {
        return 9;
    }
    config.values["video"] = BuildVideoConfig(2048);
    if (!config.SetValue("video", config.values["video"])) {
        return 10;
    }
    configured->Stop();
    return 0;
}
