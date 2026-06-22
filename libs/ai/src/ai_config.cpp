#include "ai_config.h"

#include "json_utils.h"

#include <cmath>
#include <string>
#include <vector>

namespace live_stream {
namespace ai_internal {
namespace {

constexpr std::size_t kMaxPerimeterRegions = 8;
constexpr std::size_t kMaxPerimeterRegionNameLength = 32;
constexpr std::size_t kMaxAiTasks = 4;

bool ReadOptionalStringField(const ConfigJson &object, const char *key,
                             std::string *value) {
    if (value == nullptr || key == nullptr) {
        return false;
    }
    if (!object.contains(key)) {
        value->clear();
        return true;
    }
    if (!object.at(key).is_string()) {
        return false;
    }
    *value = object.at(key).get<std::string>();
    return value->size() <= kMaxPerimeterRegionNameLength;
}

bool ParsePerimeterRegion(const ConfigJson &value,
                          AiPerimeterRegion *region) {
    if (region == nullptr || !value.is_object()) {
        return false;
    }
    AiPerimeterRegion parsed;
    if (!ReadOptionalStringField(value, "name", &parsed.name) ||
        !json_utils::ReadField(value, "x", &parsed.x, 0.0f, 1.0f) ||
        !json_utils::ReadField(value, "y", &parsed.y, 0.0f, 1.0f) ||
        !json_utils::ReadField(value, "width", &parsed.width, 0.0f, 1.0f) ||
        !json_utils::ReadField(value, "height", &parsed.height, 0.0f,
                               1.0f)) {
        return false;
    }
    if (parsed.width <= 0.0f || parsed.height <= 0.0f ||
        parsed.x + parsed.width > 1.0f || parsed.y + parsed.height > 1.0f) {
        return false;
    }
    *region = parsed;
    return true;
}

bool ParsePerimeterRegions(const ConfigJson &value,
                           std::vector<AiPerimeterRegion> *regions) {
    if (regions == nullptr || !value.is_array() ||
        value.size() > kMaxPerimeterRegions) {
        return false;
    }
    std::vector<AiPerimeterRegion> parsed_regions;
    parsed_regions.reserve(value.size());
    for (const ConfigJson &item : value) {
        AiPerimeterRegion region;
        if (!ParsePerimeterRegion(item, &region)) {
            return false;
        }
        parsed_regions.push_back(region);
    }
    *regions = parsed_regions;
    return true;
}

bool ParseOptionalPerimeterConfig(const ConfigJson &value,
                                  AiPerimeterConfig *perimeter) {
    if (perimeter == nullptr) {
        return false;
    }
    if (!value.contains("perimeter_regions")) {
        return true;
    }
    return ParsePerimeterRegions(value.at("perimeter_regions"),
                                 &perimeter->regions);
}

bool ContainsTask(const std::vector<AiModelConfig> &tasks, AiTask task) {
    for (const AiModelConfig &item : tasks) {
        if (item.task == task) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool IsFiniteConfidence(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

const char *AiBackendToString(AiBackend backend) {
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
    if (value == "perimeter_detection") {
        *task = AiTask::kPerimeterDetection;
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

bool IsValidAiTaskConfig(const AiModelConfig &config) {
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

bool IsValidAiConfig(const AiConfig &config) {
    if (config.tasks.empty() || config.tasks.size() > kMaxAiTasks) {
        return false;
    }
    std::vector<AiModelConfig> seen_tasks;
    seen_tasks.reserve(config.tasks.size());
    for (const AiModelConfig &task_config : config.tasks) {
        if (!IsValidAiTaskConfig(task_config) ||
            ContainsTask(seen_tasks, task_config.task)) {
            return false;
        }
        seen_tasks.push_back(task_config);
    }
    return true;
}

bool ParseAiTaskConfig(const ConfigJson &value, const AiModelConfig &fallback,
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
                               &config.confidence_threshold, 0.0f, 1.0f) ||
        !ParseOptionalPerimeterConfig(value, &config.perimeter)) {
        return false;
    }
    if (!IsValidAiTaskConfig(config)) {
        return false;
    }
    *parsed = config;
    return true;
}

bool ParseAiConfig(const ConfigJson &value, const AiConfig &fallback,
                   AiConfig *parsed) {
    if (parsed == nullptr || !value.is_object() || !value.contains("tasks") ||
        !value.at("tasks").is_array() ||
        value.at("tasks").size() > kMaxAiTasks) {
        return false;
    }
    AiConfig config = fallback;
    if (!json_utils::ReadField(value, "enabled", &config.enabled)) {
        return false;
    }

    std::vector<AiModelConfig> parsed_tasks;
    parsed_tasks.reserve(value.at("tasks").size());
    for (const ConfigJson &item : value.at("tasks")) {
        AiModelConfig fallback_task;
        if (item.is_object() && item.contains("task") &&
            item.at("task").is_string()) {
            AiTask task = AiTask::kObjectDetection;
            if (!ParseTask(item.at("task").get<std::string>(), &task)) {
                return false;
            }
            for (const AiModelConfig &candidate : fallback.tasks) {
                if (candidate.task == task) {
                    fallback_task = candidate;
                    break;
                }
            }
        }
        AiModelConfig task_config;
        if (!ParseAiTaskConfig(item, fallback_task, &task_config) ||
            ContainsTask(parsed_tasks, task_config.task)) {
            return false;
        }
        parsed_tasks.push_back(task_config);
    }
    config.tasks = parsed_tasks;
    if (!IsValidAiConfig(config)) {
        return false;
    }
    *parsed = config;
    return true;
}

}  // namespace ai_internal
}  // namespace live_stream
