#include "ai_config.h"

#include "json_utils.h"

#include <cmath>
#include <string>

namespace live_stream {
namespace ai_internal {

bool IsFiniteConfidence(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

const char *AiBackendName(AiBackend backend) {
    switch (backend) {
        case AiBackend::kHi3516Dv300Nnie:
            return "hisi3516dv300_nnie";
        case AiBackend::kHostStub:
            return "host_stub";
    }
    return "hisi3516dv300_nnie";
}

bool ParseBackend(const std::string &value, AiBackend *backend) {
    if (backend == nullptr) {
        return false;
    }
    if (value == "hisi3516dv300_nnie" || value == "nnie") {
        *backend = AiBackend::kHi3516Dv300Nnie;
        return true;
    }
    if (value == "host_stub") {
        *backend = AiBackend::kHostStub;
        return true;
    }
    return false;
}

bool ParseTask(const std::string &value, AiTask *task) {
    if (task == nullptr) {
        return false;
    }
    if (value == "object_detection") {
        *task = AiTask::kObjectDetection;
        return true;
    }
    if (value == "face_detection") {
        *task = AiTask::kFaceDetection;
        return true;
    }
    if (value == "motion_classification") {
        *task = AiTask::kMotionClassification;
        return true;
    }
    if (value == "occlusion_detection") {
        *task = AiTask::kOcclusionDetection;
        return true;
    }
    return false;
}

bool ParseStream(const std::string &value, StreamId *stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    if (value == "main") {
        *stream_id = StreamId::kMain;
        return true;
    }
    if (value == "sub") {
        *stream_id = StreamId::kSub;
        return true;
    }
    return false;
}

bool IsValidAiConfig(const AiModelConfig &config) {
    if (config.input_width == 0 || config.input_height == 0 ||
        config.inference_interval_ms == 0 || config.max_results == 0 ||
        !IsFiniteConfidence(config.confidence_threshold)) {
        return false;
    }
    if (!config.enabled) {
        return true;
    }
    if (config.backend == AiBackend::kHi3516Dv300Nnie &&
        config.task != AiTask::kMotionClassification &&
        config.task != AiTask::kOcclusionDetection &&
        config.model_path.empty()) {
        return false;
    }
    return config.stream_id == StreamId::kMain ||
           config.stream_id == StreamId::kSub;
}

bool ParseAiConfig(const ConfigJson &value, const AiModelConfig &fallback,
                   AiModelConfig *parsed) {
    if (parsed == nullptr || !value.is_object()) {
        return false;
    }
    AiModelConfig config = fallback;
    std::string backend;
    std::string task;
    std::string stream;
    if (!json_utils::ReadField(value, "enabled", &config.enabled) ||
        !json_utils::ReadField(value, "backend", &backend) ||
        !ParseBackend(backend, &config.backend) ||
        !json_utils::ReadField(value, "task", &task) ||
        !ParseTask(task, &config.task) ||
        !json_utils::ReadField(value, "stream", &stream) ||
        !ParseStream(stream, &config.stream_id) ||
        !json_utils::ReadField(value, "model_path", &config.model_path) ||
        !json_utils::ReadField(value, "input_width", &config.input_width, 1,
                               0xffffffffU) ||
        !json_utils::ReadField(value, "input_height", &config.input_height, 1,
                               0xffffffffU) ||
        !json_utils::ReadField(value, "inference_interval_ms",
                               &config.inference_interval_ms, 1, 0xffffffffU) ||
        !json_utils::ReadField(value, "max_results", &config.max_results, 1,
                               0xffffffffU) ||
        !json_utils::ReadField(value, "confidence_threshold",
                               &config.confidence_threshold, 0.0f, 1.0f)) {
        return false;
    }
    if (!IsValidAiConfig(config)) {
        return false;
    }
    *parsed = config;
    return true;
}

}  // namespace ai_internal
}  // namespace live_stream
