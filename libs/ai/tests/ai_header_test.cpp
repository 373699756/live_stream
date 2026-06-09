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

  bool SetValue(const std::string& name,
                const live_stream::ConfigJson& value) override {
    if (name != "ai") {
      return false;
    }
    if (attachment_.validate &&
        !attachment_.validate(value).ok) {
      return false;
    }
    if (attachment_.apply &&
        !attachment_.apply(value).ok) {
      return false;
    }
    ai_config_ = value;
    return true;
  }

  live_stream::ConfigJson GetValue(const std::string& name) override {
    if (name != "ai") {
      return live_stream::ConfigJson();
    }
    return ai_config_;
  }

  bool SetDefault(const std::string& name) override {
    return name == "ai";
  }

  live_stream::ConfigJson GetDefault(const std::string& name) override {
    if (name != "ai") {
      return live_stream::ConfigJson();
    }
    return ai_config_;
  }

  bool RestoreDefaults() override { return true; }

  bool AttachConfig(const std::string& name,
                    const live_stream::ConfigAttachment& attachment) override {
    if (name != "ai" || attached_) {
      return false;
    }
    attachment_ = attachment;
    attached_ = true;
    return true;
  }

  bool DetachConfig(const std::string& name) override {
    if (name != "ai" || !attached_) {
      return false;
    }
    attached_ = false;
    attachment_ = live_stream::ConfigAttachment();
    return true;
  }

private:
  live_stream::ConfigJson ai_config_;
  live_stream::ConfigAttachment attachment_;
  bool attached_ = false;
};

live_stream::ConfigJson DisabledAiConfig() {
  return live_stream::ConfigJson{
      {"enabled", false},
      {"tasks",
       live_stream::ConfigJson::array(
           {live_stream::ConfigJson{
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
               {"perimeter_regions", live_stream::ConfigJson::array()},
           }})},
  };
}

live_stream::AiConfig EnabledHostStubConfig() {
  live_stream::AiConfig config;
  config.enabled = true;
  live_stream::AiModelConfig task;
  task.enabled = true;
  task.backend = live_stream::AiBackend::kHostStub;
  task.task = live_stream::AiTask::kObjectDetection;
  task.stream_id = live_stream::StreamId::kMain;
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
  default_task.backend = live_stream::AiBackend::kHostStub;
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

  if (!config.SetValue("ai", DisabledAiConfig())) {
    return 4;
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
  degraded_options.default_config = EnabledHostStubConfig();
  live_stream::Ai degraded_service(degraded_options);
  if (!degraded_service.Start()) {
    return 7;
  }
  live_stream::AiStats degraded_stats = degraded_service.GetStats();
  if (!degraded_stats.enabled || degraded_stats.backend_available) {
    return 8;
  }
  std::vector<live_stream::AiTaskStatus> degraded_tasks =
      degraded_service.GetTaskStatuses();
  if (degraded_tasks.size() != 1 ||
      !degraded_tasks[0].stats.enabled ||
      degraded_tasks[0].stats.backend_available) {
    return 9;
  }
  degraded_service.Stop();

  service.Stop();
  return 0;
}
