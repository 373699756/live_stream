#include "nnie_backend_runner.h"

#include "ai_config.h"
#include "hisi_ai_platform.h"
#include "motion_backend.h"
#include "nnie_object_backend.h"
#include "occlusion_backend.h"

#include <memory>
#include <string>

namespace live_stream {
namespace ai_internal {
namespace {

class Hi3516Dv300NnieBackendRunner final : public AiBackendRunner {
public:
    const char *Name() const override { return "hisi3516dv300_nnie"; }
    bool Available() const override { return LIVE_STREAM_HAS_HISI_NNIE != 0; }

    bool Start(const AiModelConfig &config) override {
        if (!IsValidAiTaskConfig(config)) {
            return false;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        Stop();
        if (config.task == AiTask::kMotionClassification) {
            if (!motion_backend_.Start(config)) {
                return false;
            }
            model_path_ = config.model_path;
            started_ = true;
            return true;
        }
        if (config.task == AiTask::kOcclusionDetection) {
            if (!occlusion_backend_.Start(config)) {
                return false;
            }
            model_path_ = config.model_path;
            started_ = true;
            return true;
        }
        if (!object_backend_.Start(config)) {
            return false;
        }
        model_path_ = config.model_path;
        started_ = true;
        return true;
#else
        (void)config;
        return false;
#endif
    }

    void Stop() override {
#if LIVE_STREAM_HAS_HISI_NNIE
        object_backend_.Stop();
        motion_backend_.Stop();
        occlusion_backend_.Stop();
#endif
        model_path_.clear();
        started_ = false;
    }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!started_ || !frame.Valid()) {
            return result;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        if (occlusion_backend_.Started()) {
            return occlusion_backend_.Run(frame, stream_id, config);
        }
        if (motion_backend_.Started()) {
            return motion_backend_.Run(frame, stream_id, config);
        }
        return object_backend_.Run(frame, stream_id, config);
#else
        (void)config;
        return result;
#endif
    }

private:
    MotionBackend motion_backend_;
    OcclusionBackend occlusion_backend_;
    NnieObjectBackend object_backend_;
    std::string model_path_;
    bool started_ = false;
};

}  // namespace

std::shared_ptr<AiBackendRunner> CreateNnieBackendRunner() {
    return std::shared_ptr<AiBackendRunner>(new Hi3516Dv300NnieBackendRunner());
}

}  // namespace ai_internal
}  // namespace live_stream
