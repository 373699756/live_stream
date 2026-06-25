#include "ai.h"

#include "config.h"

#include <memory>
#include <string>

namespace {

class FakeConfig : public live_stream::IConfig {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::ConfigCode Set(const std::string& name,
                                  const live_stream::Json& now,
                                  live_stream::ConfigError* error) override {
        if (name != "ai") {
            return live_stream::ConfigCode::kMissing;
        }
        if (scope_.verify) {
            const live_stream::ConfigCode code =
                scope_.verify(now, error);
            if (code != live_stream::ConfigCode::kOk) {
                return code;
            }
        }
        if (scope_.apply) {
            const live_stream::ConfigCode code =
                scope_.apply(ai_config_, now, error);
            if (code != live_stream::ConfigCode::kOk) {
                return code;
            }
        }
        ai_config_ = now;
        return live_stream::ConfigCode::kOk;
    }

    live_stream::Json Get(const std::string& name) override {
        if (name != "ai") {
            return live_stream::Json();
        }
        return ai_config_;
    }

    live_stream::ConfigCode Reset(
        const std::string& name, live_stream::ConfigError*) override {
        return name == "ai" ? live_stream::ConfigCode::kOk
                            : live_stream::ConfigCode::kMissing;
    }

    live_stream::Json Default(const std::string& name) override {
        if (name != "ai") {
            return live_stream::Json();
        }
        return ai_config_;
    }

    live_stream::ConfigCode ResetAll(
        live_stream::ConfigError*) override {
        return live_stream::ConfigCode::kOk;
    }

    bool AddScope(const std::string& name,
                  const live_stream::ConfigScope& scope) override {
        if (name != "ai" || attached_) {
            return false;
        }
        scope_ = scope;
        attached_ = true;
        return true;
    }

    bool RemoveScope(const std::string& name) override {
        if (name != "ai" || !attached_) {
            return false;
        }
        attached_ = false;
        scope_ = live_stream::ConfigScope();
        return true;
    }

private:
    live_stream::Json ai_config_;
    live_stream::ConfigScope scope_;
    bool attached_ = false;
};

live_stream::Json DisabledAiConfig() {
    return live_stream::Json{
        {"enabled", false},
        {"tasks",
         live_stream::Json::array(
             {live_stream::Json{
                 {"enabled", false},
                 {"backend", "hisi3516dv300_nnie"},
                 {"task", "object_detection"},
                 {"stream", "main"},
                 {"model_path", ""},
                 {"input_width", 416},
                 {"input_height", 416},
                 {"inference_interval_ms", 200},
                 {"max_results", 16},
                 {"confidence_threshold", 0.5},
                 {"perimeter_regions", live_stream::Json::array()},
             }})},
    };
}

live_stream::Json HostStubAiConfig() {
    live_stream::Json config = DisabledAiConfig();
    config["enabled"] = true;
    config["tasks"][0]["enabled"] = true;
    config["tasks"][0]["backend"] = "host_stub";
    return config;
}

live_stream::AiConfig EnabledNnieConfig() {
    live_stream::AiConfig config;
    config.enabled = true;
    live_stream::AiModelConfig task;
    task.enabled = true;
    task.backend = live_stream::AiBackend::kHi3516Dv300Nnie;
    task.task = live_stream::AiTask::kObjectDetection;
    task.stream_id = live_stream::StreamId::kMain;
    task.model_path = "models/inst_ssd_cycle.wk";
    task.input_width = 416;
    task.input_height = 416;
    task.inference_interval_ms = 200;
    task.max_results = 16;
    task.confidence_threshold = 0.5f;
    config.tasks.push_back(task);
    return config;
}

}  // namespace

int main() {
    if (std::string(live_stream::Ai::StaticName()) != "ai") {
        return 1;
    }

    FakeConfig config;
    live_stream::AiOptions options;
    options.config = &config;
    options.default_config.enabled = false;
    live_stream::AiModelConfig default_task;
    default_task.enabled = false;
    default_task.backend = live_stream::AiBackend::kHi3516Dv300Nnie;
    default_task.task = live_stream::AiTask::kObjectDetection;
    default_task.stream_id = live_stream::StreamId::kMain;
    default_task.input_width = 416;
    default_task.input_height = 416;
    default_task.inference_interval_ms = 200;
    options.default_config.tasks.push_back(default_task);

    live_stream::Ai service(options);
    if (!service.Start()) {
        return 2;
    }
    live_stream::AiStats stats = service.GetStats();
    if (stats.enabled || stats.backend_available) {
        return 3;
    }

    if (config.Set("ai", DisabledAiConfig(), nullptr) !=
        live_stream::ConfigCode::kOk) {
        return 4;
    }
    if (config.Set("ai", HostStubAiConfig(), nullptr) ==
        live_stream::ConfigCode::kOk) {
        return 10;
    }
    live_stream::AiConfig ai_config = service.GetConfig();
    if (ai_config.tasks.empty() ||
        ai_config.tasks[0].backend !=
            live_stream::AiBackend::kHi3516Dv300Nnie) {
        return 5;
    }

    live_stream::AiInferenceResult result = service.GetLastResult();
    if (result.success || result.stream_id != live_stream::StreamId::kMain) {
        return 6;
    }

    live_stream::AiOptions degraded_options;
    degraded_options.default_config = EnabledNnieConfig();
    live_stream::Ai degraded_service(degraded_options);
    if (!degraded_service.Start()) {
        return 7;
    }
    live_stream::AiStats degraded_stats = degraded_service.GetStats();
    if (!degraded_stats.enabled || degraded_stats.backend_available) {
        return 8;
    }
    std::vector<live_stream::AiTaskInfo> degraded_tasks =
        degraded_service.GetTaskInfoList();
    if (degraded_tasks.size() != 1 ||
        !degraded_tasks[0].stats.enabled ||
        degraded_tasks[0].stats.backend_available) {
        return 9;
    }
    degraded_service.Stop();

    service.Stop();
    return 0;
}
