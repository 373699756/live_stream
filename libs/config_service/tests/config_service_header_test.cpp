#include "config_service.h"

#include "live_stream/json_utils.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace {

bool WriteText(const std::string &path, const std::string &content) {
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    file.flush();
    return file.good();
}

std::unique_ptr<live_stream::IConfigService>
CreateStartedService(const std::string &config_path,
                     const std::string &default_config_path) {
    live_stream::ConfigServiceOptions options;
    options.config_path = config_path;
    options.default_config_path = default_config_path;
    std::unique_ptr<live_stream::IConfigService> service =
        live_stream::CreateConfigService(options);
    if (!service || !service->Start()) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    const std::string config_path = "/tmp/live_stream_config_service_test.json";
    const std::string default_path =
        "/tmp/live_stream_config_service_default_test.json";
    std::remove(config_path.c_str());
    std::remove((config_path + ".tmp").c_str());
    std::remove(default_path.c_str());
    std::remove((default_path + ".tmp").c_str());

    if (!WriteText(default_path,
                   "{\"stream\":{\"bitrate\":1024,\"fps\":25},"
                   "\"image\":{\"brightness\":50}}")) {
        return 1;
    }
    if (!WriteText(config_path, "{\"stream\":{\"bitrate\":2048}}")) {
        return 2;
    }

    std::unique_ptr<live_stream::IConfigService> service =
        CreateStartedService(config_path, default_path);
    if (!service) {
        return 3;
    }

    live_stream::ConfigJson value = service->GetValue("stream");
    if (!value.is_object() || value["bitrate"] != 2048 || value["fps"] != 25) {
        return 4;
    }
    value = service->GetDefault("stream");
    if (!value.is_object() || value["bitrate"] != 1024 || value["fps"] != 25) {
        return 5;
    }

    int validate_count = 0;
    int apply_count = 0;
    int observer_count = 0;
    bool observer_value_ok = false;

    live_stream::ConfigAttachment attachment;
    attachment.validate = [&validate_count](
                              const live_stream::ConfigJson &config) {
        ++validate_count;
        int64_t bitrate = 0;
        if (!live_stream::json_utils::Load(config, "bitrate", &bitrate, 128,
                                           8192)) {
            return live_stream::ConfigResult::Failure("bitrate", "unsupported value");
        }
        return live_stream::ConfigResult::Success();
    };
    attachment.apply = [&apply_count](const live_stream::ConfigJson &config) {
        ++apply_count;
        return config["bitrate"] == 4096 || config["bitrate"] == 3072
                   ? live_stream::ConfigResult::Success()
                   : live_stream::ConfigResult::Failure("bitrate",
                                                        "apply rejected");
    };
    if (!service->AttachConfig("stream", attachment)) {
        return 6;
    }
    if (service->AttachConfig("stream", attachment)) {
        return 7;
    }

    const live_stream::ConfigObserverId observer_id = service->ObserveConfig(
        "stream", [&observer_count,
                   &observer_value_ok](const live_stream::ConfigJson &config) {
            ++observer_count;
            observer_value_ok = config["bitrate"] == 4096;
        });
    if (observer_id == 0) {
        return 8;
    }

    live_stream::ConfigJson next_stream = {
        {"bitrate", 4096},
        {"fps", 30},
        {"codec", "h264"},
    };
    if (!service->SetValue("stream", next_stream) || validate_count != 1 ||
        apply_count != 1 || observer_count != 1 || !observer_value_ok) {
        return 9;
    }

    value = service->GetValue("stream");
    if (!value.is_object() || value["bitrate"] != 4096 ||
        value["codec"] != "h264") {
        return 10;
    }

    live_stream::ConfigJson invalid_stream = {
        {"bitrate", 1},
        {"fps", 30},
    };
    if (service->SetValue("stream", invalid_stream)) {
        return 11;
    }
    const live_stream::ConfigError invalid_error =
        service->GetLastConfigError("stream");
    if (invalid_error.field != "bitrate" ||
        invalid_error.reason != "unsupported value") {
        return 12;
    }
    value = service->GetValue("stream");
    if (!value.is_object() || value["bitrate"] != 4096 || apply_count != 1 ||
        observer_count != 1) {
        return 13;
    }

    if (!service->UnobserveConfig("stream", observer_id) ||
        service->UnobserveConfig("stream", observer_id)) {
        return 14;
    }

    live_stream::ConfigJson another_stream = {
        {"bitrate", 3072},
        {"fps", 25},
        {"codec", "h265"},
    };
    if (!service->SetValue("stream", another_stream) || validate_count != 2 ||
        apply_count != 2 || observer_count != 1) {
        return 15;
    }

    if (!service->DetachConfig("stream")) {
        return 16;
    }
    if (service->SetValue("missing", live_stream::ConfigJson::object())) {
        return 17;
    }
    const live_stream::ConfigError missing_error =
        service->GetLastConfigError("missing");
    if (missing_error.reason != "config not found") {
        return 18;
    }

    if (!service->RestoreDefaults()) {
        return 19;
    }
    value = service->GetValue("stream");
    if (!value.is_object() || value["bitrate"] != 1024 || value["fps"] != 25) {
        return 20;
    }

    service->Stop();
    std::remove(config_path.c_str());
    std::remove((config_path + ".tmp").c_str());
    std::remove(default_path.c_str());
    std::remove((default_path + ".tmp").c_str());
    return 0;
}
