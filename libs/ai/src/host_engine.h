#ifndef LIVE_STREAM_AI_SRC_HOST_ENGINE_H_
#define LIVE_STREAM_AI_SRC_HOST_ENGINE_H_

#include "ai_engine.h"

#include <memory>

namespace live_stream {
namespace ai_internal {

std::shared_ptr<AiInferenceEngine> CreateHostEngine();

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_HOST_ENGINE_H_
