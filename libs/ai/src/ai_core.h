#ifndef LIVE_STREAM_AI_SRC_AI_CORE_H_
#define LIVE_STREAM_AI_SRC_AI_CORE_H_

#include "ai.h"

#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class AiCore final {
public:
    explicit AiCore(const AiOptions &options);
    ~AiCore();

    bool Start();
    void Stop();
    AiConfig GetConfig() const;
    AiStats GetStats() const;
    AiInferenceResult GetLastResult() const;
    std::vector<AiTaskStatus> GetTaskStatuses() const;
    std::vector<AiAlertRecord> ListAlerts() const;
    std::string ReadAlertImage(const std::string &id) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_CORE_H_
