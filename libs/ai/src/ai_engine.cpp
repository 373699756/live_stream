#include "ai_engine.h"

#include "host_engine.h"
#include "nnie_engine.h"

namespace live_stream {
namespace ai_internal {

std::shared_ptr<AiInferenceEngine> CreateAiEngine(AiBackend backend) {
    if (backend == AiBackend::kHostStub) {
        return CreateHostEngine();
    }
    return CreateNnieEngine();
}

}  // namespace ai_internal
}  // namespace live_stream
