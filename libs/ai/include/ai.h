#ifndef LIVE_STREAM_AI_AI_H_
#define LIVE_STREAM_AI_AI_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hisi_vendor/mpp_types.h"
#include "media/stream_types.h"

namespace live_stream {

class IAlarm;
class IConfig;
class DeviceMedia;

namespace hisisdk {
class IHisiSnapshot;
}  // namespace hisisdk

enum class AiTask {
    kObjectDetection = 0,
    kPerimeterDetection = 2,
    kMotionClassification = 3,
    kOcclusionDetection = 4,
};

enum class AiBackend {
    kHi3516Dv300Nnie = 0,
};

struct AiPerimeterRegion {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct AiPerimeterConfig {
    std::vector<AiPerimeterRegion> regions;
};

struct AiModelConfig {
    bool enabled = false;
    AiBackend backend = AiBackend::kHi3516Dv300Nnie;
    AiTask task = AiTask::kObjectDetection;
    StreamId stream_id = StreamId::kSub;
    std::string model_path;
    uint32_t input_width = 300;
    uint32_t input_height = 300;
    uint32_t inference_interval_ms = 500;
    uint32_t max_results = 16;
    float confidence_threshold = 0.5f;
    AiPerimeterConfig perimeter;
};

struct AiConfig {
    bool enabled = false;
    std::vector<AiModelConfig> tasks;
};

struct AiTaskCapability {
    AiTask task = AiTask::kObjectDetection;
    bool available = false;
    bool requires_model = false;
    std::string unavailable_reason;
    std::string default_model_path;
    uint32_t default_input_width = 300;
    uint32_t default_input_height = 300;
    uint32_t min_inference_interval_ms = 250;
    uint32_t max_inference_interval_ms = 2000;
    uint32_t default_inference_interval_ms = 500;
    uint32_t min_results = 1;
    uint32_t max_results = 32;
    uint32_t default_max_results = 16;
    float min_confidence_threshold = 0.0f;
    float max_confidence_threshold = 1.0f;
    float default_confidence_threshold = 0.5f;
    uint32_t max_perimeter_regions = 0;
    std::vector<AiBackend> supported_backends;
    std::vector<StreamId> supported_streams;
};

struct AiCapabilities {
    bool available = false;
    bool model_runtime_available = false;
    std::string model_runtime_reason;
    std::vector<AiTaskCapability> tasks;
};

struct AiDetection {
    std::string label;
    float confidence = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct AiInferenceResult {
    bool success = false;
    StreamId stream_id = StreamId::kMain;
    FrameSequence sequence = 0;
    int64_t pts_us = 0;
    std::vector<AiDetection> detections;
};

struct AiStats {
    bool enabled = false;
    bool backend_available = false;
    bool alarm_linked = false;
    int64_t last_success_time_ms = 0;
    int64_t last_failure_time_ms = 0;
    uint64_t received_frames = 0;
    uint64_t skipped_frames = 0;
    uint64_t inferences = 0;
    uint64_t failed_inferences = 0;
    uint64_t dropped_tasks = 0;
    uint32_t last_inference_time_ms = 0;
    uint32_t max_inference_time_ms = 0;
    uint32_t average_inference_time_ms = 0;
    uint32_t active_results = 0;
};

struct AiTaskInfo {
    AiModelConfig config;
    AiStats stats;
    AiInferenceResult last_result;
};

struct AiAlertRecord {
    std::string id;
    int64_t timestamp_ms = 0;
    StreamId stream_id = StreamId::kMain;
    AiTask task = AiTask::kObjectDetection;
    uint32_t detected_targets = 0;
    float max_confidence = 0.0f;
    std::vector<AiDetection> detections;
};

struct AiOptions {
    AiConfig default_config;
    IConfig* config = nullptr;
    IAlarm* alarm = nullptr;
    DeviceMedia* device = nullptr;
    hisisdk::IHisiSnapshot* snapshot = nullptr;
    std::string alert_image_dir = "build/ai_alerts";
    uint32_t max_alert_records = 100;
};

// IAiReader is the narrow read-only interface used by http and other
// cross-module callers. Ai implements it.
class IAiReader {
public:
    virtual ~IAiReader() = default;
    virtual AiCapabilities GetCapabilities() const = 0;
    virtual AiConfig GetConfig() const = 0;
    virtual AiStats GetStats() const = 0;
    virtual AiInferenceResult GetLastResult() const = 0;
    virtual std::vector<AiTaskInfo> GetTaskInfoList() const = 0;
    virtual std::vector<AiAlertRecord> ListAlerts() const = 0;
    virtual std::string ReadAlertImage(const std::string& id) const = 0;
};

class Ai : public IAiReader {
public:
    Ai();
    explicit Ai(const AiOptions& options);
    ~Ai();

    bool Start();
    void Stop();

    AiCapabilities GetCapabilities() const override;
    AiConfig GetConfig() const override;
    AiStats GetStats() const override;
    AiInferenceResult GetLastResult() const override;
    std::vector<AiTaskInfo> GetTaskInfoList() const override;
    std::vector<AiAlertRecord> ListAlerts() const override;
    std::string ReadAlertImage(const std::string& id) const override;

    static const char* StaticName();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_AI_AI_H_
