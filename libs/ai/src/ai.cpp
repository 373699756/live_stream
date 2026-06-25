#include "ai.h"

#include "ai_task_runner.h"

namespace live_stream {

struct Ai::Impl final {
    explicit Impl(const AiOptions &options) : task_runner(options) {}

    AiTaskRunner task_runner;
};

Ai::Ai() : Ai(AiOptions{}) {}

Ai::Ai(const AiOptions &options) : impl_(new Impl(options)) {}

Ai::~Ai() = default;

bool Ai::Start() { return impl_->task_runner.Start(); }

void Ai::Stop() {
    impl_->task_runner.Stop();
}

AiCapabilities Ai::GetCapabilities() const {
    return impl_->task_runner.GetCapabilities();
}

AiConfig Ai::GetConfig() const {
    return impl_->task_runner.GetConfig();
}

AiStats Ai::GetStats() const {
    return impl_->task_runner.GetStats();
}

AiInferenceResult Ai::GetLastResult() const {
    return impl_->task_runner.GetLastResult();
}

std::vector<AiTaskInfo> Ai::GetTaskInfoList() const {
    return impl_->task_runner.GetTaskInfoList();
}

std::vector<AiAlertRecord> Ai::ListAlerts() const {
    return impl_->task_runner.ListAlerts();
}

std::string Ai::ReadAlertImage(const std::string &id) const {
    return impl_->task_runner.ReadAlertImage(id);
}

const char *Ai::StaticName() { return "ai"; }

}  // namespace live_stream
