#ifndef LIVE_STREAM_AI_SRC_AI_CONFIG_H_
#define LIVE_STREAM_AI_SRC_AI_CONFIG_H_

#include "ai.h"
#include "config.h"

namespace live_stream {
namespace ai_internal {

const char *AiBackendToString(AiBackend backend);
bool IsValidAiTaskConfig(const AiModelConfig &config);
bool IsValidAiConfig(const AiConfig &config);
bool ParseAiTaskConfig(const Json &value, const AiModelConfig &fallback,
                       AiModelConfig *parsed);
bool ParseAiConfig(const Json &value, const AiConfig &fallback,
                   AiConfig *parsed);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_CONFIG_H_
