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
      {"backend", "host_stub"},
      {"task", "object_detection"},
      {"stream", "main"},
      {"model_path", ""},
      {"input_width", 416},
      {"input_height", 416},
      {"inference_interval_ms", 200},
      {"max_results", 16},
      {"confidence_threshold", 0.5},
  };
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
  options.default_config.backend = live_stream::AiBackend::kHostStub;

  live_stream::Ai service(options);
  if (!service.Start()) {
    return 2;
  }
  live_stream::AiStats stats = service.GetStats();
  if (!stats.enabled || stats.backend_available) {
    return 3;
  }

  if (!config.SetValue("ai", DisabledAiConfig())) {
    return 4;
  }
  if (service.GetConfig().backend != live_stream::AiBackend::kHostStub) {
    return 5;
  }

  live_stream::AiInferenceResult result = service.GetLastResult();
  if (result.success || result.stream_id != live_stream::StreamId::kMain) {
    return 6;
  }
  service.Stop();
  return 0;
}
