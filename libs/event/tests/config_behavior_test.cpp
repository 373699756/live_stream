#include "config.h"

#include "json_reader.h"

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

std::unique_ptr<live_stream::IConfig>
CreateStarted(const std::string &config_path,
              const std::string &default_config_path) {
    live_stream::ConfigOptions options;
    options.config_path = config_path;
    options.default_config_path = default_config_path;
    std::unique_ptr<live_stream::IConfig> service =
        live_stream::CreateConfig(options);
    if (!service || !service->Start()) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    const std::string config_path = "/tmp/live_stream_config_test.json";
    const std::string default_path =
        "/tmp/live_stream_config_default_test.json";
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

    std::unique_ptr<live_stream::IConfig> service =
        CreateStarted(config_path, default_path);
    if (!service) {
        return 3;
    }

    live_stream::Json value = service->Get("stream");
    if (!value.is_object() || value["bitrate"] != 2048 || value["fps"] != 25) {
        return 4;
    }
    value = service->Default("stream");
    if (!value.is_object() || value["bitrate"] != 1024 || value["fps"] != 25) {
        return 5;
    }

    int validate_count = 0;
    int apply_count = 0;

    live_stream::ConfigScope scope;
    scope.verify = [&validate_count](const live_stream::Json &now,
                                     live_stream::ConfigError *error) {
        ++validate_count;
        int64_t bitrate = 0;
        if (!live_stream::json_reader::ReadField(now, "bitrate", &bitrate,
                                                128, 8192)) {
            if (error != nullptr) {
                error->field = "bitrate";
                error->message = "unsupported value";
            }
            return live_stream::ConfigCode::kVerify;
        }
        return live_stream::ConfigCode::kOk;
    };
    scope.apply = [&apply_count](const live_stream::Json &prev,
                                 const live_stream::Json &now,
                                 live_stream::ConfigError *error) {
        (void)prev;
        ++apply_count;
        if (now["bitrate"] == 4096 || now["bitrate"] == 3072) {
            return live_stream::ConfigCode::kOk;
        }
        if (error != nullptr) {
            error->field = "bitrate";
            error->message = "apply rejected";
        }
        return live_stream::ConfigCode::kApply;
    };
    if (!service->AddScope("stream", scope)) {
        return 6;
    }
    if (service->AddScope("stream", scope)) {
        return 7;
    }

    live_stream::Json next_stream = {
        {"bitrate", 4096},
        {"fps", 30},
        {"codec", "h264"},
    };
    live_stream::ConfigError error;
    if (service->Set("stream", next_stream, &error) !=
            live_stream::ConfigCode::kOk ||
        validate_count != 1 || apply_count != 1) {
        return 9;
    }

    value = service->Get("stream");
    if (!value.is_object() || value["bitrate"] != 4096 ||
        value["codec"] != "h264") {
        return 10;
    }

    live_stream::Json invalid_stream = {
        {"bitrate", 1},
        {"fps", 30},
    };
    error = live_stream::ConfigError();
    if (service->Set("stream", invalid_stream, &error) !=
            live_stream::ConfigCode::kVerify ||
        error.field != "bitrate") {
        return 11;
    }
    value = service->Get("stream");
    if (!value.is_object() || value["bitrate"] != 4096 || apply_count != 1) {
        return 13;
    }

    live_stream::Json another_stream = {
        {"bitrate", 3072},
        {"fps", 25},
        {"codec", "h265"},
    };
    if (service->Set("stream", another_stream, &error) !=
            live_stream::ConfigCode::kOk ||
        validate_count != 2 || apply_count != 2) {
        return 15;
    }

    if (!service->RemoveScope("stream")) {
        return 16;
    }
    if (service->Set("missing", live_stream::Json::object(), &error) ==
        live_stream::ConfigCode::kOk) {
        return 17;
    }

    if (service->ResetAll(&error) != live_stream::ConfigCode::kOk) {
        return 19;
    }
    value = service->Get("stream");
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
