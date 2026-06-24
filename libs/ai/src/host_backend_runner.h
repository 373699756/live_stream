#ifndef LIVE_STREAM_AI_SRC_HOST_BACKEND_RUNNER_H_
#define LIVE_STREAM_AI_SRC_HOST_BACKEND_RUNNER_H_

#include "ai_backend_runner.h"

#include <memory>

namespace live_stream {
namespace ai_internal {

std::shared_ptr<AiBackendRunner> CreateHostBackendRunner();

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_HOST_BACKEND_RUNNER_H_
