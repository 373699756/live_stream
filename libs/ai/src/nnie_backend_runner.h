#ifndef LIVE_STREAM_AI_SRC_NNIE_BACKEND_RUNNER_H_
#define LIVE_STREAM_AI_SRC_NNIE_BACKEND_RUNNER_H_

#include "ai_backend_runner.h"

#include <memory>

namespace live_stream {
namespace ai_internal {

std::shared_ptr<AiBackendRunner> CreateNnieBackendRunner();

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_NNIE_BACKEND_RUNNER_H_
