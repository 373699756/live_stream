#include "http_ai_health.h"

#include "ai.h"
#include "config.h"
#include "json_reader.h"

namespace live_stream {

bool IsAiConfigEnabled(IConfig *config) {
    if (config == nullptr) {
        return false;
    }
    Json ai_config = config->Get("ai");
    bool enabled = false;
    return ai_config.is_object() &&
           json_reader::ReadField(ai_config, "enabled", &enabled) &&
           enabled;
}

bool IsAiHealthy(const IAiReader *ai) {
    if (ai == nullptr) {
        return false;
    }
    const AiStats stats = ai->GetStats();
    return !stats.enabled || stats.backend_available;
}

}  // namespace live_stream
