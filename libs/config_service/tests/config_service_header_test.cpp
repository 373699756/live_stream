#include "config_service.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace {

bool WriteText(const std::string& path, const std::string& content) {
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    file.flush();
    return file.good();
}

std::unique_ptr<live_stream::IConfigService> CreateStartedService(
    const std::string& config_path,
    const std::string& default_config_path) {
    live_stream::ConfigServiceOptions options;
    options.config_path = config_path;
    options.default_config_path = default_config_path;
    std::unique_ptr<live_stream::IConfigService> service =
        live_stream::CreateConfigService(options);
    if (!service || service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    const std::string config_path =
        "/tmp/live_stream_config_service_test.json";
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

    live_stream::ConfigJson value;
    if (service->GetValue("stream", &value) != infra::Status::kOk ||
        value["bitrate"] != 2048 || value["fps"] != 25) {
        return 4;
    }
    if (service->GetDefault("stream", &value) != infra::Status::kOk ||
        value["bitrate"] != 1024 || value["fps"] != 25) {
        return 5;
    }

    int verify_count = 0;
    if (service->RegisterVerify(
            "stream",
            [&verify_count](const live_stream::ConfigJson& config) {
                ++verify_count;
                if (!config.contains("bitrate") || config["bitrate"] < 128) {
                    return infra::Status::kInvalidParam;
                }
                return infra::Status::kOk;
            }) != infra::Status::kOk) {
        return 6;
    }

    int apply_count = 0;
    bool apply_value_ok = false;
    if (service->RegisterApply(
            "stream",
            [&apply_count, &apply_value_ok](
                const live_stream::ConfigJson& config) {
                ++apply_count;
                apply_value_ok = config["bitrate"] == 4096;
                return infra::Status::kOk;
            }) != infra::Status::kOk) {
        return 7;
    }

    live_stream::ConfigJson next_stream = {
        {"bitrate", 4096},
        {"fps", 30},
        {"codec", "h264"},
    };
    if (service->SetValue("stream", next_stream) != infra::Status::kOk ||
        verify_count != 1 || apply_count != 1 || !apply_value_ok) {
        return 8;
    }

    if (service->GetValue("stream", &value) != infra::Status::kOk ||
        value["bitrate"] != 4096 || value["codec"] != "h264") {
        return 9;
    }

    live_stream::ConfigJson invalid_stream = {
        {"bitrate", 1},
        {"fps", 30},
    };
    if (service->SetValue("stream", invalid_stream) !=
        infra::Status::kInvalidParam) {
        return 10;
    }
    if (service->GetValue("stream", &value) != infra::Status::kOk ||
        value["bitrate"] != 4096 || apply_count != 1) {
        return 11;
    }

    if (service->SetValue("stream.bitrate", 512) !=
        infra::Status::kInvalidParam ||
        service->GetValue("All", &value) != infra::Status::kNotFound ||
        service->SetValue("missing", live_stream::ConfigJson::object()) !=
            infra::Status::kNotFound) {
        return 12;
    }

    if (service->RestoreDefaults() != infra::Status::kOk ||
        service->GetValue("stream", &value) != infra::Status::kOk ||
        value["bitrate"] != 1024 || value["fps"] != 25) {
        return 13;
    }

    service->Stop();
    service->Deinit();
    std::remove(config_path.c_str());
    std::remove((config_path + ".tmp").c_str());
    std::remove(default_path.c_str());
    std::remove((default_path + ".tmp").c_str());
    return 0;
}
