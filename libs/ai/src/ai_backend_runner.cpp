#include "ai_backend_runner.h"

#include "host_backend_runner.h"
#include "nnie_backend_runner.h"

namespace live_stream {
namespace ai_internal {

std::shared_ptr<AiBackendRunner> CreateAiBackendRunner(AiBackend backend) {
    if (backend == AiBackend::kHostStub) {
        return CreateHostBackendRunner();
    }
    return CreateNnieBackendRunner();
}

}  // namespace ai_internal
}  // namespace live_stream
