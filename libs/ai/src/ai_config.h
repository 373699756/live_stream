#ifndef LIVE_STREAM_AI_SRC_AI_CONFIG_H_
#define LIVE_STREAM_AI_SRC_AI_CONFIG_H_

#include "ai.h"
#include "config.h"

namespace live_stream {
namespace ai_internal {

const char *AiBackendName(AiBackend backend);
bool IsValidAiConfig(const AiModelConfig &config);
bool ParseAiConfig(const ConfigJson &value, const AiModelConfig &fallback,
                   AiModelConfig *parsed);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_CONFIG_H_
