#ifndef LIVE_STREAM_AI_SRC_AI_TASK_RUNNER_H_
#define LIVE_STREAM_AI_SRC_AI_TASK_RUNNER_H_

#include "ai.h"

#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class AiTaskRunner final {
public:
    explicit AiTaskRunner(const AiOptions &options);
    ~AiTaskRunner();

    bool Start();
    void Stop();
    AiCapabilities GetCapabilities() const;
    AiConfig GetConfig() const;
    AiStats GetStats() const;
    AiInferenceResult GetLastResult() const;
    std::vector<AiTaskInfo> GetTaskInfoList() const;
    std::vector<AiAlertRecord> ListAlerts() const;
    std::string ReadAlertImage(const std::string &id) const;

private:
    struct TaskRunnerInfo;
    std::unique_ptr<TaskRunnerInfo> info_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_TASK_RUNNER_H_
