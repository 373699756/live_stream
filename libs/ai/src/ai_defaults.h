#ifndef LIVE_STREAM_AI_SRC_AI_DEFAULTS_H_
#define LIVE_STREAM_AI_SRC_AI_DEFAULTS_H_

#include "ai.h"

namespace live_stream {
namespace ai_internal {

AiModelConfig DefaultAiTaskConfig(AiTask task);
AiConfig DefaultAiConfig();
AiCapabilities BuildAiCapabilities();

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_DEFAULTS_H_
