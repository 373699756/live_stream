#include "ai.h"

#include "ai_runtime.h"

namespace live_stream {

struct Ai::Impl final {
    explicit Impl(const AiOptions &options) : core(options) {}

    AiRuntime core;
};

Ai::Ai() : Ai(AiOptions{}) {}

Ai::Ai(const AiOptions &options) : impl_(new Impl(options)) {}

Ai::~Ai() = default;

bool Ai::Start() { return impl_ != nullptr && impl_->core.Start(); }

void Ai::Stop() {
    if (impl_) {
        impl_->core.Stop();
    }
}

AiConfig Ai::GetConfig() const {
    return impl_ != nullptr ? impl_->core.GetConfig() : AiConfig{};
}

AiStats Ai::GetStats() const {
    return impl_ != nullptr ? impl_->core.GetStats() : AiStats{};
}

AiInferenceResult Ai::GetLastResult() const {
    return impl_ != nullptr ? impl_->core.GetLastResult()
                            : AiInferenceResult{};
}

std::vector<AiTaskStatus> Ai::GetTaskStatuses() const {
    return impl_ != nullptr ? impl_->core.GetTaskStatuses()
                            : std::vector<AiTaskStatus>();
}

std::vector<AiAlertRecord> Ai::ListAlerts() const {
    return impl_ != nullptr ? impl_->core.ListAlerts()
                            : std::vector<AiAlertRecord>();
}

std::string Ai::ReadAlertImage(const std::string &id) const {
    return impl_ != nullptr ? impl_->core.ReadAlertImage(id) : std::string();
}

const char *Ai::StaticName() { return "ai"; }

}  // namespace live_stream
