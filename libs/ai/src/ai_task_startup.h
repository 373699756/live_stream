#ifndef LIVE_STREAM_AI_SRC_AI_TASK_STARTUP_H_
#define LIVE_STREAM_AI_SRC_AI_TASK_STARTUP_H_

#include "ai_task_workers.h"

#include <memory>
#include <vector>

namespace live_stream {
namespace ai_internal {

class AiTaskStartup final {
public:
    bool Prepare(const std::shared_ptr<AiTaskWorker> &task_worker,
                 uint32_t executor_queue_capacity);
    void Stop();

    const std::shared_ptr<AiTaskWorker> &TaskWorker() const;
    const std::shared_ptr<AiBackendRunner> &BackendRunner() const;
    std::unique_ptr<event::Executor> TakeExecutor();
    void ReleaseBackendRunner();

private:
    std::shared_ptr<AiTaskWorker> task_worker_;
    std::shared_ptr<AiBackendRunner> backend_runner_;
    std::unique_ptr<event::Executor> executor_;
};

void StopStoppedAiTask(StoppedAiTask *stopped);
void StopStoppedAiTasks(std::vector<StoppedAiTask> *stopped_tasks);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_TASK_STARTUP_H_
