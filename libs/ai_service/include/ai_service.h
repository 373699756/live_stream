#ifndef LIVE_STREAM_AI_SERVICE_H_
#define LIVE_STREAM_AI_SERVICE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/mpp_types.h"
#include "media/stream_types.h"

namespace live_stream {

class IConfigService;
class IMediaService;
class ISnapshotView;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

enum class AiTask {
    kObjectDetection = 0,
    kFaceDetection,
    kMotionClassification,
};

enum class AiBackend {
    kHi3516Dv300Nnie = 0,
    kHostStub,
};

struct AiModelConfig {
    bool enabled = false;
    AiBackend backend = AiBackend::kHi3516Dv300Nnie;
    AiTask task = AiTask::kObjectDetection;
    StreamId stream_id = StreamId::kMain;
    std::string model_path;
    uint32_t input_width = 416;
    uint32_t input_height = 416;
    uint32_t inference_interval_ms = 200;
    uint32_t max_results = 16;
    float confidence_threshold = 0.5f;
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

struct AiServiceStats {
    bool enabled = false;
    bool backend_available = false;
    uint64_t received_frames = 0;
    uint64_t skipped_frames = 0;
    uint64_t inference_count = 0;
    uint64_t inference_failed_count = 0;
    uint64_t dropped_tasks = 0;
    uint32_t active_results = 0;
};

struct AiAlertRecord {
    std::string id;
    int64_t timestamp_ms = 0;
    StreamId stream_id = StreamId::kMain;
    AiTask task = AiTask::kObjectDetection;
    uint32_t detection_count = 0;
    float max_confidence = 0.0f;
    std::vector<AiDetection> detections;
};

struct AiServiceOptions {
    AiModelConfig default_config;
    IConfigService* config_service = nullptr;
    IMediaService* media_service = nullptr;
    ISnapshotView* snapshot_service = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSdk* sdk = nullptr;
    std::string alert_image_dir = "build/runtime/ai_alerts";
    uint32_t max_alert_records = 100;
};

// IAiView is the narrow interface consumed by HttpService (and other
// cross-module consumers). AiService implements it.
class IAiView {
public:
    virtual ~IAiView() = default;
    virtual AiModelConfig GetConfig() const = 0;
    virtual AiServiceStats GetStats() const = 0;
    virtual AiInferenceResult GetLastResult() const = 0;
    virtual std::vector<AiAlertRecord> ListAlerts() const = 0;
    virtual std::string ReadAlertImage(const std::string& id) const = 0;
};

class AiService : public IAiView {
public:
    AiService();
    explicit AiService(const AiServiceOptions& options);
    ~AiService();

    bool Start();
    void Stop();

    AiModelConfig GetConfig() const override;
    AiServiceStats GetStats() const override;
    AiInferenceResult GetLastResult() const override;
    std::vector<AiAlertRecord> ListAlerts() const override;
    std::string ReadAlertImage(const std::string& id) const override;

    static const char* StaticName();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SERVICE_H_
