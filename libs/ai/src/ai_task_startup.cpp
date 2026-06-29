#include "ai_task_startup.h"

#include "ai_backend_runner.h"
#include "ai_config.h"

#include "infra/log.h"

namespace live_stream {
namespace ai_internal {

bool AiTaskStartup::Prepare(
    const std::shared_ptr<AiTaskWorker> &task_worker,
    uint32_t executor_queue_capacity) {
    Stop();
    if (!task_worker) {
        return false;
    }
    const AiModelConfig &task_config = task_worker->config;
    Info("ai",
         "AI backend start begin: backend=%s task=%d model=%s stream=%d "
         "input=%ux%u interval=%u threshold=%.3f max=%u",
         AiBackendToString(task_config.backend),
         static_cast<int>(task_config.task),
         task_config.model_path.c_str(),
         static_cast<int>(task_config.stream_id),
         static_cast<unsigned int>(task_config.input_width),
         static_cast<unsigned int>(task_config.input_height),
         static_cast<unsigned int>(task_config.inference_interval_ms),
         static_cast<double>(task_config.confidence_threshold),
         static_cast<unsigned int>(task_config.max_results));

    std::shared_ptr<AiBackendRunner> next_backend_runner =
        CreateAiBackendRunner(task_config.backend);
    if (!next_backend_runner) {
        Error("ai", "Create AI backend failed: backend=%s task=%d",
              AiBackendToString(task_config.backend),
              static_cast<int>(task_config.task));
        return false;
    }
    Info("ai", "AI backend created: backend=%s task=%d available=%d",
         AiBackendToString(task_config.backend),
         static_cast<int>(task_config.task),
         next_backend_runner->Available() ? 1 : 0);
    if (!next_backend_runner->Available() ||
        !next_backend_runner->Start(task_config)) {
        Error("ai", "Start AI backend failed: backend=%s task=%d model=%s",
              AiBackendToString(task_config.backend),
              static_cast<int>(task_config.task),
              task_config.model_path.c_str());
        next_backend_runner->Stop();
        return false;
    }
    Info("ai", "AI backend start done: backend=%s task=%d",
         AiBackendToString(task_config.backend),
         static_cast<int>(task_config.task));

    std::unique_ptr<event::Executor> next_executor(new event::Executor());
    event::ExecutorOptions executor_options;
    executor_options.workers = 1;
    executor_options.queue_capacity = executor_queue_capacity;
    if (!next_executor->Start(executor_options)) {
        next_backend_runner->Stop();
        return false;
    }

    task_worker_ = task_worker;
    backend_runner_ = next_backend_runner;
    executor_ = std::move(next_executor);
    return true;
}

void AiTaskStartup::Stop() {
    if (executor_) {
        executor_->Stop(event::StopMode::kDiscard);
    }
    if (backend_runner_) {
        backend_runner_->Stop();
    }
    executor_.reset();
    backend_runner_.reset();
    task_worker_.reset();
}

const std::shared_ptr<AiTaskWorker> &AiTaskStartup::TaskWorker() const {
    return task_worker_;
}

const std::shared_ptr<AiBackendRunner> &AiTaskStartup::BackendRunner() const {
    return backend_runner_;
}

std::unique_ptr<event::Executor> AiTaskStartup::TakeExecutor() {
    return std::move(executor_);
}

void AiTaskStartup::ReleaseBackendRunner() { backend_runner_.reset(); }

void StopStoppedAiTask(StoppedAiTask *stopped) {
    if (stopped == nullptr) {
        return;
    }
    if (stopped->executor) {
        stopped->executor->Stop(event::StopMode::kDiscard);
    }
    if (stopped->backend_runner) {
        stopped->backend_runner->Stop();
    }
    stopped->executor.reset();
    stopped->backend_runner.reset();
}

void StopStoppedAiTasks(std::vector<StoppedAiTask> *stopped_tasks) {
    if (stopped_tasks == nullptr) {
        return;
    }
    for (StoppedAiTask &stopped : *stopped_tasks) {
        StopStoppedAiTask(&stopped);
    }
    stopped_tasks->clear();
}

}  // namespace ai_internal
}  // namespace live_stream
