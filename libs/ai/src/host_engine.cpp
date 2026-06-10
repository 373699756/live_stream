#include "host_engine.h"

#include "ai_config.h"

namespace live_stream {
namespace ai_internal {
namespace {

class HostStubAiEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "host_stub"; }
    bool Available() const override { return true; }

    bool Start(const AiModelConfig &config) override {
        started_ = IsValidAiTaskConfig(config);
        if (started_) {
            sequence_ = 0;
        }
        return started_;
    }

    void Stop() override { started_ = false; }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        AiInferenceResult result;
        result.success = started_ && frame.buffer && frame.size > 0;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!result.success) {
            return result;
        }
        result.sequence = ++sequence_;
        if (config.max_results == 0) {
            return result;
        }
        AiDetection detection = DetectionForTask(config.task);
        if (detection.confidence >= config.confidence_threshold) {
            result.detections.push_back(detection);
        }
        return result;
    }

private:
    AiDetection DetectionForTask(AiTask task) const {
        AiDetection detection;
        switch (task) {
            case AiTask::kMotionClassification:
                detection.label = "motion";
                detection.confidence = 0.79f;
                detection.x = 0.12f;
                detection.y = 0.18f;
                detection.width = 0.46f;
                detection.height = 0.34f;
                return detection;
            case AiTask::kOcclusionDetection:
                detection.label = "occlusion";
                detection.confidence = 0.88f;
                detection.x = 0.0f;
                detection.y = 0.0f;
                detection.width = 1.0f;
                detection.height = 1.0f;
                return detection;
            case AiTask::kPerimeterDetection:
                detection.label = "person";
                detection.confidence = 0.84f;
                detection.x = 0.58f;
                detection.y = 0.34f;
                detection.width = 0.18f;
                detection.height = 0.36f;
                return detection;
            case AiTask::kObjectDetection:
                detection.label = "person";
                detection.confidence = 0.86f;
                detection.x = 0.18f;
                detection.y = 0.22f;
                detection.width = 0.2f;
                detection.height = 0.46f;
                return detection;
        }
        return detection;
    }

    FrameSequence sequence_ = 0;
    bool started_ = false;
};

}  // namespace

std::shared_ptr<AiInferenceEngine> CreateHostEngine() {
    return std::shared_ptr<AiInferenceEngine>(new HostStubAiEngine());
}

}  // namespace ai_internal
}  // namespace live_stream
