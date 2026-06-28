#ifndef LIVE_STREAM_AI_SRC_AI_TASK_WORKERS_H_
#define LIVE_STREAM_AI_SRC_AI_TASK_WORKERS_H_

#include "ai.h"
#include "ai_backend_runner.h"

#include "alarm.h"
#include "event.h"

#include <memory>
#include <vector>

namespace live_stream {
namespace ai_internal {

struct AiTaskWorker final {
    explicit AiTaskWorker(const AiModelConfig &task_config);

    AiModelConfig config;
    std::shared_ptr<AiBackendRunner> backend_runner;
    std::unique_ptr<event::Executor> executor;
    AiInferenceResult last_result;
    AiStats stats;
    uint64_t inference_time_total_ms = 0;
    int64_t last_result_time_ms = 0;
    int64_t last_alert_ms = 0;
    bool running = false;
};

struct StoppedAiTask {
    std::unique_ptr<event::Executor> executor;
    std::shared_ptr<AiBackendRunner> backend_runner;
};

class AiTaskWorkers final {
public:
    void Rebuild(const AiConfig &config, bool alarm_linked);
    void ApplyConfigDiff(const AiConfig &next_config,
                         bool alarm_linked,
                         std::vector<StoppedAiTask> *stopped_tasks);
    void StopAll(std::vector<StoppedAiTask> *stopped_tasks);
    void StopWorker(const std::shared_ptr<AiTaskWorker> &task_worker,
                    std::vector<StoppedAiTask> *stopped_tasks);
    std::shared_ptr<AiTaskWorker> Find(AiTask task) const;
    const std::vector<std::shared_ptr<AiTaskWorker>> &Items() const;
    std::vector<std::shared_ptr<AiTaskWorker>> EnabledToStart(
        const AiConfig &config,
        bool alarm_linked);
    void MarkAllEnabledBackendsUnavailable(bool alarm_linked);
    AiStats TaskStats(const AiConfig &config,
                      const std::shared_ptr<AiTaskWorker> &task_worker,
                      bool alarm_linked) const;
    AiStats SummaryStats(const AiConfig &config, bool alarm_linked) const;
    AiInferenceResult LatestResult() const;
    AlarmInput BuildAlarmInput() const;

private:
    std::vector<std::shared_ptr<AiTaskWorker>> workers_;
};

bool SameAiTaskConfig(const AiModelConfig &lhs, const AiModelConfig &rhs);
const char *AiTaskAlarmName(AiTask task);
bool IsAiAlertResultActive(const AiInferenceResult &result);
uint32_t ClampAiInferenceTime(int64_t inference_time_ms);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_TASK_WORKERS_H_
