#include "ai_config_binding.h"

#include "ai_config.h"

namespace live_stream {
namespace ai_internal {

namespace {
void FillInvalidAiConfigError(ConfigError *error) {
    if (error == nullptr) {
        return;
    }
    error->field.clear();
    error->message = "invalid ai config";
}
}  // namespace

AiConfigBinding::AiConfigBinding(IConfig *config) : config_(config) {}

AiConfigBinding::~AiConfigBinding() { Detach(); }

bool AiConfigBinding::Attach(
    const CurrentConfigReader &read_current_config,
    const ConfigApplier &apply_config) {
    if (config_ == nullptr || attached_) {
        return true;
    }

    ConfigScope config_scope;
    config_scope.verify =
        [read_current_config](const Json &now, ConfigError *error) {
            AiConfig parsed;
            if (read_current_config &&
                ParseAiConfig(now, read_current_config(), &parsed)) {
                return ConfigCode::kOk;
            }
            FillInvalidAiConfigError(error);
            return ConfigCode::kVerify;
        };
    config_scope.apply =
        [read_current_config, apply_config](const Json &prev,
                                            const Json &now,
                                            ConfigError *error) {
            (void)prev;
            AiConfig parsed;
            if (!read_current_config ||
                !ParseAiConfig(now, read_current_config(), &parsed)) {
                FillInvalidAiConfigError(error);
                return ConfigCode::kVerify;
            }
            if (apply_config && apply_config(parsed)) {
                return ConfigCode::kOk;
            }
            if (error != nullptr) {
                error->field.clear();
                error->message = "apply ai config failed";
            }
            return ConfigCode::kApply;
        };
    if (!config_->AddScope("ai", config_scope)) {
        return false;
    }
    attached_ = true;
    return true;
}

void AiConfigBinding::Detach() {
    if (!attached_ || config_ == nullptr) {
        return;
    }
    attached_ = false;
    static_cast<void>(config_->RemoveScope("ai"));
}

bool AiConfigBinding::LoadInitial(const AiConfig &current_config,
                                  AiConfig *loaded_config) const {
    if (loaded_config == nullptr) {
        return false;
    }
    *loaded_config = current_config;
    if (config_ == nullptr) {
        return true;
    }
    Json ai_config = config_->Get("ai");
    if (!ai_config.is_object()) {
        return true;
    }
    return ParseAiConfig(ai_config, current_config, loaded_config);
}

}  // namespace ai_internal
}  // namespace live_stream
