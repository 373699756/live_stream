#include "ai_runtime.h"

#include "ai_config.h"
#include "ai_engine.h"
#include "alarm.h"
#include "config.h"
#include "device_media.h"
#include "infra/executor.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "infra/time.h"
#include "snapshot.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr uint32_t kDefaultExecutorQueueCapacity = 8;
constexpr uint32_t kCaptureStopPollMs = 50;
constexpr int64_t kMinAlertIntervalMs = 1000;

using ai_internal::AiBackendName;
using ai_internal::AiInferenceEngine;
using ai_internal::CreateAiEngine;
using ai_internal::IsValidAiConfig;
using ai_internal::ParseAiConfig;

MppChannel VpssChannelForStream(const MediaChannels &channels,
                                StreamId stream_id) {
    return stream_id == StreamId::kSub ? channels.sub_vpss : channels.vpss;
}

hisisdk::Size YuvSizeForStream(const MediaChannels &channels,
                               StreamId stream_id) {
    const VideoSize size = stream_id == StreamId::kSub ? channels.sub_size
                                                       : channels.main_size;
    return hisisdk::Size{size.width, size.height};
}

bool HasAlertDetections(const AiInferenceResult &result) {
    return result.success && !result.detections.empty();
}

float MaxConfidence(const std::vector<AiDetection> &detections) {
    float max_confidence = 0.0f;
    for (const AiDetection &detection : detections) {
        if (detection.confidence > max_confidence) {
            max_confidence = detection.confidence;
        }
    }
    return max_confidence;
}

std::string AlertImagePath(const std::string &dir, const std::string &id) {
    return infra::Path::Join(dir, id + ".jpg");
}

const char *TaskAlarmName(AiTask task) {
    switch (task) {
        case AiTask::kFaceDetection:
            return "face";
        case AiTask::kPerimeterDetection:
            return "perimeter";
        case AiTask::kMotionClassification:
            return "motion";
        case AiTask::kOcclusionDetection:
            return "occlusion";
        case AiTask::kObjectDetection:
            return "object";
    }
    return "ai";
}

bool IsPerimeterTargetLabel(const std::string &label) {
    return label == "person" || label == "car" || label == "bus" ||
           label == "truck" || label == "motorbike" || label == "bicycle" ||
           label == "vehicle";
}

bool DetectionCenterInsideRegion(const AiDetection &detection,
                                 const AiPerimeterRegion &region) {
    const float center_x = detection.x + detection.width * 0.5f;
    const float center_y = detection.y + detection.height * 0.5f;
    return center_x >= region.x && center_x <= region.x + region.width &&
           center_y >= region.y && center_y <= region.y + region.height;
}

bool DetectionInsidePerimeter(const AiDetection &detection,
                              const AiPerimeterConfig &perimeter) {
    if (perimeter.regions.empty()) {
        return true;
    }
    for (const AiPerimeterRegion &region : perimeter.regions) {
        if (DetectionCenterInsideRegion(detection, region)) {
            return true;
        }
    }
    return false;
}

AiInferenceResult FilterPerimeterDetections(
    const AiInferenceResult &result, const AiPerimeterConfig &perimeter) {
    AiInferenceResult filtered = result;
    filtered.detections.clear();
    for (const AiDetection &detection : result.detections) {
        if (IsPerimeterTargetLabel(detection.label) &&
            DetectionInsidePerimeter(detection, perimeter)) {
            filtered.detections.push_back(detection);
        }
    }
    return filtered;
}

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.size >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

}  // namespace

struct AiRuntime::State final {
    explicit State(const AiOptions &service_options)
        : options(service_options), config(service_options.default_config) {
        if (options.max_alert_records == 0) {
            options.max_alert_records = 100;
        }
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!IsValidAiConfig(config)) {
            return false;
        }
        if (options.config != nullptr && !config_attached) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                AiModelConfig current_config;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    current_config = config;
                }
                AiModelConfig parsed;
                return ParseAiConfig(value, current_config, &parsed)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid ai config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                AiModelConfig current_config;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    current_config = config;
                }
                AiModelConfig parsed;
                if (!ParseAiConfig(value, current_config, &parsed)) {
                    return ConfigResult::Failure("", "invalid ai config");
                }
                return ApplyConfig(parsed)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure(
                                 "", "apply ai config failed");
            };
            if (!options.config->AttachConfig("ai", attachment)) {
                return false;
            }
            config_attached = true;
        }
        if (options.config != nullptr) {
            ConfigJson ai_config = options.config->GetValue("ai");
            if (ai_config.is_object()) {
                AiModelConfig parsed;
                if (!ParseAiConfig(ai_config, config, &parsed)) {
                    return false;
                }
                config = parsed;
            }
        }
        return true;
    }

    bool Start() {
        if (!Prepare()) {
            return false;
        }

        AiModelConfig start_config;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (started) {
                return true;
            }
            start_config = config;
            stats.enabled = start_config.enabled;
            stats.backend_available = false;
            if (!start_config.enabled) {
                started = true;
                return true;
            }
            started = true;
            if (!StartInferenceLocked(start_config)) {
                started = false;
                stats.enabled = false;
                stats.backend_available = false;
                return false;
            }
        }
        Info("ai", "AI started: backend=%s stream=%d",
             AiBackendName(start_config.backend),
             static_cast<int>(start_config.stream_id));
        return true;
    }

    void Stop() {
        std::unique_ptr<infra::Executor> stopped_executor;
        std::shared_ptr<AiInferenceEngine> stopped_engine;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started && !executor && !engine) {
                return;
            }
            started = false;
            inference_running = false;
            stopped_executor = std::move(executor);
            stopped_engine = std::move(engine);
            stats.backend_available = false;
        }
        if (stopped_executor) {
            stopped_executor->Stop(infra::StopMode::kDiscard);
        }
        if (stopped_engine) {
            stopped_engine->Stop();
        }
        ClearAlarmInput();
    }

    void Release() {
        Stop();
        bool detach_config = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            detach_config = config_attached;
            config_attached = false;
        }
        if (detach_config && options.config != nullptr) {
            static_cast<void>(options.config->DetachConfig("ai"));
        }
    }

    void CaptureLoop() {
        int64_t next_inference_ms = infra::Time::MonotonicMillis();
        while (true) {
            AiModelConfig run_config;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!started || !inference_running || !config.enabled) {
                    return;
                }
                run_config = config;
            }
            const int64_t now_ms = infra::Time::MonotonicMillis();
            if (now_ms < next_inference_ms) {
                const int64_t wait_ms = next_inference_ms - now_ms;
                infra::Time::SleepMillis(static_cast<uint32_t>(
                    std::min<int64_t>(wait_ms, kCaptureStopPollMs)));
                continue;
            }
            next_inference_ms =
                infra::Time::MonotonicMillis() +
                static_cast<int64_t>(run_config.inference_interval_ms);
            hisisdk::YuvFrame frame = options.sdk->CaptureYuvFrame(
                VpssChannelForStream(options.media_channels,
                                     run_config.stream_id),
                YuvSizeForStream(options.media_channels,
                                 run_config.stream_id),
                run_config.inference_interval_ms);
            if (!frame.buffer || frame.size == 0) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++stats.skipped_frames;
                    ++stats.inference_failed_count;
                    stats.last_failure_time_ms = infra::Time::SystemTimeMillis();
                }
                ClearAlarmInput();
                continue;
            }
            RunInference(frame, run_config);
        }
    }

    void RunInference(const hisisdk::YuvFrame &frame,
                      const AiModelConfig &run_config) {
        std::shared_ptr<AiInferenceEngine> run_engine;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !inference_running || !engine) {
                ++stats.inference_failed_count;
                return;
            }
            ++stats.received_frames;
            run_engine = engine;
        }

        const int64_t inference_start_ms = infra::Time::MonotonicMillis();
        AiInferenceResult result =
            run_engine->Run(frame, run_config.stream_id, run_config);
        const int64_t inference_time_ms =
            infra::Time::MonotonicMillis() - inference_start_ms;
        if (run_config.task == AiTask::kPerimeterDetection) {
            result = FilterPerimeterDetections(result, run_config.perimeter);
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !inference_running) {
                return;
            }
            if (result.success) {
                ++stats.inference_count;
                stats.last_success_time_ms = infra::Time::SystemTimeMillis();
                UpdateInferenceTimeStatsLocked(inference_time_ms);
            } else {
                ++stats.inference_failed_count;
                stats.last_failure_time_ms = infra::Time::SystemTimeMillis();
            }
            last_result = result;
            stats.active_results =
                static_cast<uint32_t>(last_result.detections.size());
        }
        UpdateAlarmInput(result, run_config);
        MaybeSaveAlert(result, run_config);
    }

    void MaybeSaveAlert(const AiInferenceResult &result,
                        const AiModelConfig &run_config) {
        if (!HasAlertDetections(result) ||
            options.snapshot == nullptr) {
            return;
        }
        const int64_t now_ms = infra::Time::SystemTimeMillis();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !inference_running ||
                now_ms - last_alert_ms < kMinAlertIntervalMs) {
                return;
            }
            last_alert_ms = now_ms;
        }

        CaptureRequest request;
        request.stream_id = run_config.stream_id;
        request.include_thumbnail = false;
        SnapshotFrame frame = options.snapshot->Capture(request);
        if (!LooksLikeJpeg(frame)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        if (!infra::Path::MakeDirs(options.alert_image_dir)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        const std::string id =
            std::to_string(now_ms) + "-" + std::to_string(next_alert_id++);
        const uint8_t *data = frame.PayloadData();
        std::string image;
        image.assign(reinterpret_cast<const char *>(data), frame.size);
        if (!infra::File::WriteAll(AlertImagePath(options.alert_image_dir, id),
                                   image)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        AiAlertRecord alert;
        alert.id = id;
        alert.timestamp_ms = now_ms;
        alert.stream_id = result.stream_id;
        alert.task = run_config.task;
        alert.detection_count =
            static_cast<uint32_t>(result.detections.size());
        alert.max_confidence = MaxConfidence(result.detections);
        alert.detections = result.detections;
        AddAlert(alert);
    }

    void UpdateAlarmInput(const AiInferenceResult &result,
                          const AiModelConfig &run_config) {
        if (options.alarm == nullptr) {
            return;
        }
        AlarmInput input;
        input.source = AlarmSource::kAiDetection;
        input.active = HasAlertDetections(result);
        input.value = static_cast<int32_t>(result.detections.size());
        if (input.active) {
            input.message = std::string("ai_") + TaskAlarmName(run_config.task);
        }
        if (!options.alarm->InjectAlarmInput(input)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
        }
    }

    bool ApplyConfig(const AiModelConfig &next_config) {
        if (!IsValidAiConfig(next_config)) {
            return false;
        }

        AiModelConfig previous_config;
        bool service_started = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            previous_config = config;
            service_started = started;
            if (!service_started) {
                config = next_config;
                stats.enabled = next_config.enabled;
                stats.backend_available = false;
                ClearLastResultLocked();
                return true;
            }
        }

        StopInference();

        {
            std::lock_guard<std::mutex> lock(mutex);
            config = next_config;
            stats.enabled = next_config.enabled;
            stats.backend_available = false;
            ClearLastResultLocked();
            if (!started || !next_config.enabled) {
                return true;
            }
            if (StartInferenceLocked(next_config)) {
                Info("ai", "AI config applied: backend=%s stream=%d",
                     AiBackendName(next_config.backend),
                     static_cast<int>(next_config.stream_id));
                return true;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            config = previous_config;
            stats.enabled = previous_config.enabled;
            stats.backend_available = false;
            ClearLastResultLocked();
            if (!started || !previous_config.enabled) {
                return false;
            }
            if (StartInferenceLocked(previous_config)) {
                Error(
                    "ai",
                    "Apply AI config failed, previous backend restored");
                return false;
            }
        }

        Error("ai",
              "Apply AI config failed, restore previous backend "
              "failed");
        return false;
    }

    bool StartInferenceLocked(const AiModelConfig &start_config) {
        if (options.device_media == nullptr || options.sdk == nullptr ||
            !options.device_media->IsStarted()) {
            return false;
        }

        std::shared_ptr<AiInferenceEngine> next_engine =
            CreateAiEngine(start_config.backend);
        if (!next_engine || !next_engine->Available() ||
            !next_engine->Start(start_config)) {
            Error("ai", "Start AI backend failed: backend=%s model=%s",
                  AiBackendName(start_config.backend),
                  start_config.model_path.c_str());
            if (next_engine) {
                next_engine->Stop();
            }
            return false;
        }

        std::unique_ptr<infra::Executor> next_executor(new infra::Executor());
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = 1;
        executor_options.queue_capacity = kDefaultExecutorQueueCapacity;
        if (!next_executor->Start(executor_options)) {
            next_engine->Stop();
            return false;
        }
        engine = next_engine;
        executor = std::move(next_executor);
        inference_running = true;
        stats.backend_available = engine->Available();
        if (!executor->Post([this]() { CaptureLoop(); })) {
            std::unique_ptr<infra::Executor> failed_executor =
                std::move(executor);
            std::shared_ptr<AiInferenceEngine> failed_engine =
                std::move(engine);
            inference_running = false;
            stats.backend_available = false;
            failed_executor->Stop(infra::StopMode::kDiscard);
            failed_engine->Stop();
            return false;
        }
        return true;
    }

    void StopInference() {
        std::unique_ptr<infra::Executor> stopped_executor;
        std::shared_ptr<AiInferenceEngine> stopped_engine;
        {
            std::lock_guard<std::mutex> lock(mutex);
            inference_running = false;
            stopped_executor = std::move(executor);
            stopped_engine = std::move(engine);
            stats.backend_available = false;
        }
        if (stopped_executor) {
            stopped_executor->Stop(infra::StopMode::kDiscard);
        }
        if (stopped_engine) {
            stopped_engine->Stop();
        }
        ClearAlarmInput();
    }

    void ClearAlarmInput() {
        if (options.alarm == nullptr) {
            return;
        }
        AlarmInput input;
        input.source = AlarmSource::kAiDetection;
        input.active = false;
        static_cast<void>(options.alarm->InjectAlarmInput(input));
    }

    void ClearLastResultLocked() {
        last_result = AiInferenceResult{};
        stats.active_results = 0;
    }

    void UpdateInferenceTimeStatsLocked(int64_t inference_time_ms) {
        const uint32_t clamped_time_ms =
            inference_time_ms <= 0
                ? 0
                : static_cast<uint32_t>(std::min<int64_t>(
                      inference_time_ms,
                      static_cast<int64_t>(
                          std::numeric_limits<uint32_t>::max())));
        stats.last_inference_time_ms = clamped_time_ms;
        stats.max_inference_time_ms =
            std::max(stats.max_inference_time_ms, clamped_time_ms);
        inference_time_total_ms += clamped_time_ms;
        if (stats.inference_count != 0) {
            stats.average_inference_time_ms =
                static_cast<uint32_t>(inference_time_total_ms /
                                      stats.inference_count);
        }
    }

    void AddAlert(const AiAlertRecord &alert) {
        std::string expired_image_path;
        {
            std::lock_guard<std::mutex> lock(mutex);
            alerts.push_back(alert);
            while (alerts.size() > options.max_alert_records) {
                expired_image_path =
                    AlertImagePath(options.alert_image_dir, alerts.front().id);
                alerts.erase(alerts.begin());
            }
        }
        if (!expired_image_path.empty()) {
            static_cast<void>(infra::File::Remove(expired_image_path));
        }
    }

    AiOptions options;
    AiModelConfig config;
    std::shared_ptr<AiInferenceEngine> engine;
    std::unique_ptr<infra::Executor> executor;
    AiInferenceResult last_result;
    AiStats stats;
    std::vector<AiAlertRecord> alerts;
    uint64_t inference_time_total_ms = 0;
    uint64_t next_alert_id = 1;
    int64_t last_alert_ms = 0;
    bool config_attached = false;
    bool started = false;
    bool inference_running = false;
    mutable std::mutex mutex;
};

AiRuntime::AiRuntime(const AiOptions &options) : state_(new State(options)) {}

AiRuntime::~AiRuntime() {
    if (state_) {
        state_->Release();
    }
}

bool AiRuntime::Start() { return state_ != nullptr && state_->Start(); }

void AiRuntime::Stop() {
    if (state_) {
        state_->Stop();
    }
}

AiModelConfig AiRuntime::GetConfig() const {
    if (!state_) {
        return AiModelConfig{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->config;
}

AiStats AiRuntime::GetStats() const {
    if (!state_) {
        return AiStats{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    AiStats stats = state_->stats;
    stats.enabled = state_->config.enabled;
    stats.backend_available = state_->engine && state_->engine->Available();
    stats.alarm_linked = state_->options.alarm != nullptr;
    return stats;
}

AiInferenceResult AiRuntime::GetLastResult() const {
    if (!state_) {
        return AiInferenceResult{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->last_result;
}

std::vector<AiAlertRecord> AiRuntime::ListAlerts() const {
    if (!state_) {
        return std::vector<AiAlertRecord>();
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<AiAlertRecord> alerts = state_->alerts;
    std::reverse(alerts.begin(), alerts.end());
    return alerts;
}

std::string AiRuntime::ReadAlertImage(const std::string &id) const {
    if (!state_ || id.empty()) {
        return std::string();
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        const auto iter = std::find_if(
            state_->alerts.begin(), state_->alerts.end(),
            [&id](const AiAlertRecord &alert) { return alert.id == id; });
        if (iter == state_->alerts.end()) {
            return std::string();
        }
    }
    return infra::File::ReadAll(AlertImagePath(state_->options.alert_image_dir,
                                               id));
}

}  // namespace live_stream
