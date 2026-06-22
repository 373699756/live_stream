#include "config.h"

#include "json_utils.h"

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

    live_stream::ConfigJson value = service->Get("stream");
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
    scope.verify = [&validate_count](const live_stream::ConfigJson &now,
                                     live_stream::ConfigIssue *issue) {
        ++validate_count;
        int64_t bitrate = 0;
        if (!live_stream::json_utils::ReadField(now, "bitrate", &bitrate,
                                                128, 8192)) {
            if (issue != nullptr) {
                issue->field = "bitrate";
                issue->reason = "unsupported value";
            }
            return live_stream::ConfigStatus::kVerifyFailed;
        }
        return live_stream::ConfigStatus::kOk;
    };
    scope.apply = [&apply_count](const live_stream::ConfigJson &prev,
                                 const live_stream::ConfigJson &now,
                                 live_stream::ConfigIssue *issue) {
        (void)prev;
        ++apply_count;
        if (now["bitrate"] == 4096 || now["bitrate"] == 3072) {
            return live_stream::ConfigStatus::kOk;
        }
        if (issue != nullptr) {
            issue->field = "bitrate";
            issue->reason = "apply rejected";
        }
        return live_stream::ConfigStatus::kApplyFailed;
    };
    if (!service->AddScope("stream", scope)) {
        return 6;
    }
    if (service->AddScope("stream", scope)) {
        return 7;
    }

    live_stream::ConfigJson next_stream = {
        {"bitrate", 4096},
        {"fps", 30},
        {"codec", "h264"},
    };
    live_stream::ConfigIssue issue;
    if (service->Set("stream", next_stream, &issue) !=
            live_stream::ConfigStatus::kOk ||
        validate_count != 1 || apply_count != 1) {
        return 9;
    }

    value = service->Get("stream");
    if (!value.is_object() || value["bitrate"] != 4096 ||
        value["codec"] != "h264") {
        return 10;
    }

    live_stream::ConfigJson invalid_stream = {
        {"bitrate", 1},
        {"fps", 30},
    };
    issue = live_stream::ConfigIssue();
    if (service->Set("stream", invalid_stream, &issue) !=
            live_stream::ConfigStatus::kVerifyFailed ||
        issue.field != "bitrate") {
        return 11;
    }
    value = service->Get("stream");
    if (!value.is_object() || value["bitrate"] != 4096 || apply_count != 1) {
        return 13;
    }

    live_stream::ConfigJson another_stream = {
        {"bitrate", 3072},
        {"fps", 25},
        {"codec", "h265"},
    };
    if (service->Set("stream", another_stream, &issue) !=
            live_stream::ConfigStatus::kOk ||
        validate_count != 2 || apply_count != 2) {
        return 15;
    }

    if (!service->RemoveScope("stream")) {
        return 16;
    }
    if (service->Set("missing", live_stream::ConfigJson::object(), &issue) ==
        live_stream::ConfigStatus::kOk) {
        return 17;
    }

    if (service->ResetAll(&issue) != live_stream::ConfigStatus::kOk) {
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
