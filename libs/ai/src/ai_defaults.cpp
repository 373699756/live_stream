#include "ai_defaults.h"

#include "ai_model_paths.h"
#include "hisi_ai_platform.h"

namespace live_stream {
namespace ai_internal {
namespace {

constexpr uint32_t kMaxPerimeterRegions = 8;
constexpr uint32_t kDefaultAiInputWidth = 300;
constexpr uint32_t kDefaultAiInputHeight = 300;
constexpr uint32_t kMinInferenceIntervalMs = 250;
constexpr uint32_t kMaxInferenceIntervalMs = 2000;
constexpr uint32_t kDefaultInferenceIntervalMs = 500;
constexpr uint32_t kMinAiResults = 1;
constexpr uint32_t kMaxAiResults = 32;
constexpr uint32_t kDefaultAiResults = 16;
constexpr float kMinAiConfidence = 0.0f;
constexpr float kMaxAiConfidence = 1.0f;
constexpr float kDefaultAiConfidence = 0.5f;
constexpr const char *kDefaultAiModelPath = "models/inst_ssd_cycle.wk";

bool AiTaskRequiresModel(AiTask task) {
    return task == AiTask::kObjectDetection ||
           task == AiTask::kPerimeterDetection;
}

bool DefaultAiModelAvailable() {
    return AiModelFileExists(kDefaultAiModelPath);
}

AiTaskCapability BuildTaskCapability(AiTask task, bool runtime_available,
                                     bool default_model_available) {
    AiTaskCapability capability;
    capability.task = task;
    capability.requires_model = AiTaskRequiresModel(task);
    capability.available =
        runtime_available &&
        (!capability.requires_model || default_model_available);
    capability.default_model_path =
        capability.requires_model ? kDefaultAiModelPath : "";
    capability.default_input_width = kDefaultAiInputWidth;
    capability.default_input_height = kDefaultAiInputHeight;
    capability.min_inference_interval_ms = kMinInferenceIntervalMs;
    capability.max_inference_interval_ms = kMaxInferenceIntervalMs;
    capability.default_inference_interval_ms = kDefaultInferenceIntervalMs;
    capability.min_results = kMinAiResults;
    capability.max_results = kMaxAiResults;
    capability.default_max_results = kDefaultAiResults;
    capability.min_confidence_threshold = kMinAiConfidence;
    capability.max_confidence_threshold = kMaxAiConfidence;
    capability.default_confidence_threshold = kDefaultAiConfidence;
    capability.max_perimeter_regions =
        task == AiTask::kPerimeterDetection ? kMaxPerimeterRegions : 0;
    capability.supported_backends.push_back(AiBackend::kHi3516Dv300Nnie);
    capability.supported_streams.push_back(StreamId::kSub);
    capability.supported_streams.push_back(StreamId::kMain);
    if (!capability.available) {
        capability.unavailable_reason = !runtime_available
                                            ? "hisi_ai_runtime_unavailable"
                                            : "ai_model_not_deployed";
    }
    return capability;
}

}  // namespace

AiModelConfig DefaultAiTaskConfig(AiTask task) {
    AiModelConfig config;
    config.enabled = false;
    config.backend = AiBackend::kHi3516Dv300Nnie;
    config.task = task;
    config.stream_id = StreamId::kSub;
    config.input_width = kDefaultAiInputWidth;
    config.input_height = kDefaultAiInputHeight;
    config.inference_interval_ms = kDefaultInferenceIntervalMs;
    config.max_results = kDefaultAiResults;
    config.confidence_threshold = kDefaultAiConfidence;
    if (AiTaskRequiresModel(task)) {
        config.model_path = kDefaultAiModelPath;
    }
    return config;
}

AiConfig DefaultAiConfig() {
    AiConfig config;
    config.enabled = false;
    config.tasks.push_back(DefaultAiTaskConfig(AiTask::kObjectDetection));
    config.tasks.push_back(DefaultAiTaskConfig(AiTask::kPerimeterDetection));
    config.tasks.push_back(DefaultAiTaskConfig(AiTask::kMotionClassification));
    config.tasks.push_back(DefaultAiTaskConfig(AiTask::kOcclusionDetection));
    return config;
}

AiCapabilities BuildAiCapabilities() {
    AiCapabilities capabilities;
    const bool runtime_available = LIVE_STREAM_HAS_HISI_NNIE != 0;
    const bool default_model_available =
        runtime_available && DefaultAiModelAvailable();
    capabilities.model_runtime_available = default_model_available;
    capabilities.available = runtime_available;
    if (!runtime_available) {
        capabilities.model_runtime_reason =
            "hisi_nnie_ive_runtime_unavailable";
    } else if (!default_model_available) {
        capabilities.model_runtime_reason = "ai_model_not_deployed";
    }
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kObjectDetection, runtime_available,
        default_model_available));
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kPerimeterDetection, runtime_available,
        default_model_available));
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kMotionClassification, runtime_available,
        default_model_available));
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kOcclusionDetection, runtime_available,
        default_model_available));
    return capabilities;
}

}  // namespace ai_internal
}  // namespace live_stream
