#ifndef LIVE_STREAM_AI_SRC_AI_BACKEND_RUNNER_H_
#define LIVE_STREAM_AI_SRC_AI_BACKEND_RUNNER_H_

#include "ai.h"
#include "hisisdk/hisi_sdk.h"

#include <memory>

namespace live_stream {
namespace ai_internal {

class AiBackendRunner {
public:
    virtual ~AiBackendRunner() = default;

    virtual const char *Name() const = 0;
    virtual bool Available() const = 0;
    virtual bool Start(const AiModelConfig &config) = 0;
    virtual void Stop() = 0;
    virtual AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                                  StreamId stream_id,
                                  const AiModelConfig &config) = 0;
};

std::shared_ptr<AiBackendRunner> CreateAiBackendRunner(AiBackend backend);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_BACKEND_RUNNER_H_
