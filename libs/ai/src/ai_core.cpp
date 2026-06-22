#include "ai_core.h"

#include "ai_config.h"
#include "ai_engine.h"
#include "alarm.h"
#include "config.h"
#include "device.h"
#include "hisi_ai_platform.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "infra/time.h"
#include "perimeter_filter.h"

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
constexpr uint32_t kAlertExecutorQueueCapacity = 8;
constexpr uint32_t kCaptureStopPollMs = 50;
constexpr int64_t kMinAlertIntervalMs = 1000;
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

using ai_internal::AiBackendToString;
using ai_internal::AiInferenceEngine;
using ai_internal::CreateAiEngine;
using ai_internal::FilterPerimeterDetections;
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

bool IsAlertResultActive(const AiInferenceResult &result) {
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

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.size >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

AiModelConfig DefaultTaskConfig(AiTask task) {
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
    if (task == AiTask::kObjectDetection ||
        task == AiTask::kPerimeterDetection) {
        config.model_path = kDefaultAiModelPath;
    }
    return config;
}

AiConfig DefaultAiConfig() {
    AiConfig config;
    config.enabled = false;
    config.tasks.push_back(DefaultTaskConfig(AiTask::kObjectDetection));
    config.tasks.push_back(DefaultTaskConfig(AiTask::kPerimeterDetection));
    config.tasks.push_back(DefaultTaskConfig(AiTask::kMotionClassification));
    config.tasks.push_back(DefaultTaskConfig(AiTask::kOcclusionDetection));
    return config;
}

uint32_t ClampInferenceTime(int64_t inference_time_ms) {
    if (inference_time_ms <= 0) {
        return 0;
    }
    return static_cast<uint32_t>(std::min<int64_t>(
        inference_time_ms,
        static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
}

bool AiTaskRequiresModel(AiTask task) {
    return task == AiTask::kObjectDetection ||
           task == AiTask::kPerimeterDetection;
}

AiTaskCapability BuildTaskCapability(AiTask task, bool runtime_available) {
    AiTaskCapability capability;
    capability.task = task;
    capability.available = runtime_available;
    capability.requires_model = AiTaskRequiresModel(task);
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
        capability.unavailable_reason =
            "hisi_ai_runtime_unavailable";
    }
    return capability;
}

AiCapabilities BuildAiCapabilities() {
    AiCapabilities capabilities;
    capabilities.model_runtime_available = LIVE_STREAM_HAS_HISI_NNIE != 0;
    capabilities.available = capabilities.model_runtime_available;
    if (!capabilities.model_runtime_available) {
        capabilities.model_runtime_reason =
            "hisi_nnie_ive_runtime_unavailable";
    }
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kObjectDetection, capabilities.model_runtime_available));
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kPerimeterDetection, capabilities.model_runtime_available));
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kMotionClassification, capabilities.model_runtime_available));
    capabilities.tasks.push_back(BuildTaskCapability(
        AiTask::kOcclusionDetection, capabilities.model_runtime_available));
    return capabilities;
}

}  // namespace

struct AiTaskWorker final {
    explicit AiTaskWorker(const AiModelConfig &task_config)
        : config(task_config) {}

    AiModelConfig config;
    std::shared_ptr<AiInferenceEngine> engine;
    std::unique_ptr<event::Executor> executor;
    AiInferenceResult last_result;
    AiStats stats;
    uint64_t inference_time_total_ms = 0;
    int64_t last_result_time_ms = 0;
    int64_t last_alert_ms = 0;
    bool running = false;
};

struct PendingAlertCapture {
    AiInferenceResult result;
    AiModelConfig config;
    std::shared_ptr<AiTaskWorker> task_worker;
    int64_t timestamp_ms = 0;
};

struct StoppedAiTask {
    std::unique_ptr<event::Executor> executor;
    std::shared_ptr<AiInferenceEngine> engine;
};

struct AiTaskStartup {
    std::shared_ptr<AiTaskWorker> task_worker;
    std::shared_ptr<AiInferenceEngine> engine;
    std::unique_ptr<event::Executor> executor;
};

struct AiCore::State final {
    explicit State(const AiOptions &service_options)
        : options(service_options), config(service_options.default_config) {
        if (options.max_alert_records == 0) {
            options.max_alert_records = 100;
        }
        if (!IsValidAiConfig(config)) {
            config = DefaultAiConfig();
        }
        RebuildTaskWorkersLocked();
    }

    bool Prepare() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!IsValidAiConfig(config)) {
                return false;
            }
            if (options.config != nullptr && !config_attached) {
                ConfigScope config_scope;
                config_scope.verify = [this](const ConfigJson &now,
                                             ConfigIssue *issue) {
                    AiConfig current_config;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        current_config = config;
                    }
                    AiConfig parsed;
                    if (ParseAiConfig(now, current_config, &parsed)) {
                        return ConfigStatus::kOk;
                    }
                    if (issue != nullptr) {
                        issue->field.clear();
                        issue->reason = "invalid ai config";
                    }
                    return ConfigStatus::kVerifyFailed;
                };
                config_scope.apply = [this](const ConfigJson &prev,
                                            const ConfigJson &now,
                                            ConfigIssue *issue) {
                    (void)prev;
                    AiConfig current_config;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        current_config = config;
                    }
                    AiConfig parsed;
                    if (!ParseAiConfig(now, current_config, &parsed)) {
                        if (issue != nullptr) {
                            issue->field.clear();
                            issue->reason = "invalid ai config";
                        }
                        return ConfigStatus::kVerifyFailed;
                    }
                    if (ApplyConfig(parsed)) {
                        return ConfigStatus::kOk;
                    }
                    if (issue != nullptr) {
                        issue->field.clear();
                        issue->reason = "apply ai config failed";
                    }
                    return ConfigStatus::kApplyFailed;
                };
                if (!options.config->AddScope("ai", config_scope)) {
                    return false;
                }
                config_attached = true;
            }
        }

        if (options.config != nullptr) {
            ConfigJson ai_config = options.config->Get("ai");
            if (ai_config.is_object()) {
                AiConfig current_config;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    current_config = config;
                }
                AiConfig parsed;
                if (!ParseAiConfig(ai_config, current_config, &parsed)) {
                    return false;
                }
                std::lock_guard<std::mutex> lock(mutex);
                config = parsed;
                RebuildTaskWorkersLocked();
            }
        }
        return true;
    }

    bool Start() {
        if (!Prepare()) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (started) {
                return true;
            }
            started = true;
            RebuildTaskWorkersLocked();
        }

        StartConfiguredTaskWorkers();

        AiStats summary = SnapshotSummaryStats();
        std::size_t configured_task_count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            configured_task_count = task_workers.size();
        }
        Info("ai", "AI started: enabled=%d tasks=%u",
             summary.enabled ? 1 : 0,
             static_cast<unsigned int>(configured_task_count));
        return true;
    }

    void Stop() {
        std::vector<StoppedAiTask> stopped_tasks;
        std::shared_ptr<event::Executor> stopped_alert_executor;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started && task_workers.empty() && !alert_executor) {
                return;
            }
            started = false;
            StopTaskWorkersLocked(&stopped_tasks);
            stopped_alert_executor = std::move(alert_executor);
        }
        StopStoppedAiTasks(&stopped_tasks);
        StopAlertExecutor(stopped_alert_executor);
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
            static_cast<void>(options.config->RemoveScope("ai"));
        }
    }

    void CaptureLoop(std::shared_ptr<AiTaskWorker> task_worker) {
        int64_t next_inference_ms = infra::Time::MonotonicMillis();
        while (true) {
            AiModelConfig run_config;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!CanTaskRunLocked(task_worker)) {
                    return;
                }
                run_config = task_worker->config;
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

            hisisdk::YuvFrame frame;
            {
                std::lock_guard<std::mutex> capture_lock(capture_mutex);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!CanTaskRunLocked(task_worker)) {
                        return;
                    }
                }
                frame = options.sdk->CaptureYuvFrame(
                    VpssChannelForStream(options.media_channels,
                                         run_config.stream_id),
                    YuvSizeForStream(options.media_channels,
                                     run_config.stream_id),
                    run_config.inference_interval_ms);
            }

            if (!frame.buffer || frame.size == 0) {
                MarkCaptureFailure(task_worker, run_config);
                PublishAlarmInputForState(task_worker);
                continue;
            }

            PendingAlertCapture pending_alert;
            if (RunInference(frame, task_worker, run_config,
                             &pending_alert)) {
                PostAlertCapture(pending_alert);
            }
        }
    }

    bool RunInference(const hisisdk::YuvFrame &frame,
                      const std::shared_ptr<AiTaskWorker> &task_worker,
                      const AiModelConfig &run_config,
                      PendingAlertCapture *pending_alert) {
        std::shared_ptr<AiInferenceEngine> run_engine;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!CanTaskRunLocked(task_worker) || !task_worker->engine) {
                ++task_worker->stats.inference_failed_count;
                task_worker->stats.last_failure_time_ms =
                    infra::Time::SystemTimeMillis();
                return false;
            }
            ++task_worker->stats.received_frames;
            run_engine = task_worker->engine;
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
            if (!CanTaskRunLocked(task_worker)) {
                return false;
            }
            if (result.success) {
                ++task_worker->stats.inference_count;
                task_worker->stats.last_success_time_ms =
                    infra::Time::SystemTimeMillis();
                UpdateInferenceTimeStatsLocked(task_worker,
                                               inference_time_ms);
            } else {
                ++task_worker->stats.inference_failed_count;
                task_worker->stats.last_failure_time_ms =
                    infra::Time::SystemTimeMillis();
            }
            task_worker->last_result = result;
            task_worker->last_result_time_ms =
                infra::Time::MonotonicMillis();
            task_worker->stats.active_results =
                IsAlertResultActive(result)
                    ? static_cast<uint32_t>(result.detections.size())
                    : 0;
        }

        PublishAlarmInputForState(task_worker);
        return PrepareAlertCapture(result, run_config, task_worker,
                                   pending_alert);
    }

    bool PrepareAlertCapture(
        const AiInferenceResult &result,
        const AiModelConfig &run_config,
        const std::shared_ptr<AiTaskWorker> &task_worker,
        PendingAlertCapture *pending_alert) {
        if (!IsAlertResultActive(result) || options.device == nullptr ||
            pending_alert == nullptr) {
            return false;
        }
        const int64_t now_ms = infra::Time::SystemTimeMillis();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!CanTaskRunLocked(task_worker) ||
                now_ms - task_worker->last_alert_ms < kMinAlertIntervalMs) {
                return false;
            }
            task_worker->last_alert_ms = now_ms;
        }
        pending_alert->result = result;
        pending_alert->config = run_config;
        pending_alert->task_worker = task_worker;
        pending_alert->timestamp_ms = now_ms;
        return true;
    }

    void PostAlertCapture(PendingAlertCapture pending_alert) {
        std::shared_ptr<event::Executor> executor_snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !alert_executor) {
                return;
            }
            executor_snapshot = alert_executor;
        }
        if (executor_snapshot->Post([this, pending_alert]() {
                SaveAlertCapture(pending_alert);
            }) != event::EventStatus::kOk) {
            IncrementDroppedTasks(pending_alert.task_worker);
        }
    }

    void SaveAlertCapture(const PendingAlertCapture &pending_alert) {
        SnapshotRequest request;
        request.stream_id = pending_alert.config.stream_id;
        request.include_thumbnail = false;
        SnapshotFrame frame = options.device->CaptureSnapshot(request);
        if (!LooksLikeJpeg(frame)) {
            IncrementDroppedTasks(pending_alert.task_worker);
            return;
        }

        if (!infra::Path::MakeDirs(options.alert_image_dir)) {
            IncrementDroppedTasks(pending_alert.task_worker);
            return;
        }

        const std::string id =
            std::to_string(pending_alert.timestamp_ms) + "-" +
            std::to_string(NextAlertId());
        const uint8_t *data = frame.PayloadData();
        std::string image;
        image.assign(reinterpret_cast<const char *>(data), frame.size);
        if (!infra::File::WriteAll(AlertImagePath(options.alert_image_dir, id),
                                   image)) {
            IncrementDroppedTasks(pending_alert.task_worker);
            return;
        }

        AiAlertRecord alert;
        alert.id = id;
        alert.timestamp_ms = pending_alert.timestamp_ms;
        alert.stream_id = pending_alert.result.stream_id;
        alert.task = pending_alert.config.task;
        alert.detection_count =
            static_cast<uint32_t>(pending_alert.result.detections.size());
        alert.max_confidence = MaxConfidence(pending_alert.result.detections);
        alert.detections = pending_alert.result.detections;
        AddAlert(alert);
    }

    bool ApplyConfig(const AiConfig &next_config) {
        if (!IsValidAiConfig(next_config)) {
            return false;
        }

        uint32_t enabled_task_count = 0;
        for (const AiModelConfig &task_config : next_config.tasks) {
            if (task_config.enabled) {
                ++enabled_task_count;
            }
        }
        Info("ai", "AI config apply begin: enabled=%d tasks=%u enabled=%u",
             next_config.enabled ? 1 : 0,
             static_cast<unsigned int>(next_config.tasks.size()),
             static_cast<unsigned int>(enabled_task_count));

        bool service_started = false;
        std::vector<StoppedAiTask> stopped_tasks;
        std::shared_ptr<event::Executor> stopped_alert_executor;
        {
            std::lock_guard<std::mutex> lock(mutex);
            service_started = started;
            StopTaskWorkersLocked(&stopped_tasks);
            stopped_alert_executor = std::move(alert_executor);
            config = next_config;
            RebuildTaskWorkersLocked();
        }
        StopStoppedAiTasks(&stopped_tasks);
        StopAlertExecutor(stopped_alert_executor);

        if (!service_started) {
            return true;
        }

        StartConfiguredTaskWorkers();
        PublishAlarmInputForState(nullptr);
        Info("ai", "AI config applied: enabled=%d tasks=%u",
             next_config.enabled ? 1 : 0,
             static_cast<unsigned int>(next_config.tasks.size()));
        if (!next_config.enabled) {
            ClearAlarmInput();
        }
        return true;
    }

    void StartConfiguredTaskWorkers() {
        std::vector<std::shared_ptr<AiTaskWorker>> enabled_task_workers;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !config.enabled) {
                return;
            }
            if (options.device == nullptr || options.sdk == nullptr ||
                !options.device->IsStarted()) {
                Error("ai", "AI startup skipped: device media or sdk unavailable");
                MarkAllEnabledTaskBackendsUnavailableLocked();
                return;
            }
            if (!EnsureAlertExecutorLocked()) {
                Error("ai", "AI startup skipped: alert executor unavailable");
                MarkAllEnabledTaskBackendsUnavailableLocked();
                return;
            }
            for (const std::shared_ptr<AiTaskWorker> &task_worker :
                 task_workers) {
                if (!task_worker->config.enabled) {
                    task_worker->stats.enabled = false;
                    continue;
                }
                task_worker->stats.enabled = true;
                task_worker->stats.backend_available = false;
                enabled_task_workers.push_back(task_worker);
            }
        }

        Info("ai", "AI enabled task startup count=%u",
             static_cast<unsigned int>(enabled_task_workers.size()));
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             enabled_task_workers) {
            AiTaskStartup startup;
            if (!BuildAiTaskStartup(task_worker, &startup)) {
                MarkTaskBackendUnavailable(task_worker);
                continue;
            }

            StoppedAiTask failed_task;
            if (!CommitAiTaskStartup(&startup, &failed_task)) {
                StopAiTaskStartup(&startup);
                StopStoppedAiTask(&failed_task);
            }
        }
    }

    bool BuildAiTaskStartup(
        const std::shared_ptr<AiTaskWorker> &task_worker,
        AiTaskStartup *startup) {
        if (!task_worker || startup == nullptr) {
            return false;
        }
        const AiModelConfig &task_config = task_worker->config;
        Info("ai",
             "AI backend start begin: backend=%s task=%d model=%s stream=%d "
             "input=%ux%u interval=%u threshold=%.3f max=%u",
             AiBackendToString(task_config.backend),
             static_cast<int>(task_config.task),
             task_config.model_path.c_str(),
             static_cast<int>(task_config.stream_id),
             static_cast<unsigned int>(task_config.input_width),
             static_cast<unsigned int>(task_config.input_height),
             static_cast<unsigned int>(task_config.inference_interval_ms),
             static_cast<double>(task_config.confidence_threshold),
             static_cast<unsigned int>(task_config.max_results));
        std::shared_ptr<AiInferenceEngine> next_engine =
            CreateAiEngine(task_config.backend);
        if (!next_engine) {
            Error("ai", "Create AI backend failed: backend=%s task=%d",
                  AiBackendToString(task_config.backend),
                  static_cast<int>(task_config.task));
            return false;
        }
        Info("ai", "AI backend created: backend=%s task=%d available=%d",
             AiBackendToString(task_config.backend),
             static_cast<int>(task_config.task),
             next_engine->Available() ? 1 : 0);
        if (!next_engine->Available() || !next_engine->Start(task_config)) {
            Error("ai", "Start AI backend failed: backend=%s task=%d model=%s",
                  AiBackendToString(task_config.backend),
                  static_cast<int>(task_config.task),
                  task_config.model_path.c_str());
            next_engine->Stop();
            return false;
        }
        Info("ai", "AI backend start done: backend=%s task=%d",
             AiBackendToString(task_config.backend),
             static_cast<int>(task_config.task));

        std::unique_ptr<event::Executor> next_executor(new event::Executor());
        event::ExecutorOptions executor_options;
        executor_options.worker_count = 1;
        executor_options.queue_capacity = kDefaultExecutorQueueCapacity;
        if (!next_executor->Start(executor_options)) {
            next_engine->Stop();
            return false;
        }

        startup->task_worker = task_worker;
        startup->engine = next_engine;
        startup->executor = std::move(next_executor);
        return true;
    }

    bool CommitAiTaskStartup(AiTaskStartup *startup,
                             StoppedAiTask *failed_task) {
        if (startup == nullptr || !startup->task_worker || !startup->engine ||
            !startup->executor) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex);
        std::shared_ptr<AiTaskWorker> task_worker = startup->task_worker;
        if (!CanStartTaskWorkerLocked(task_worker)) {
            return false;
        }

        task_worker->engine = startup->engine;
        task_worker->executor = std::move(startup->executor);
        task_worker->stats.enabled = true;
        task_worker->stats.backend_available = startup->engine->Available();
        task_worker->running = true;
        startup->engine.reset();
        if (task_worker->executor->Post([this, task_worker]() {
                CaptureLoop(task_worker);
            }) != event::EventStatus::kOk) {
            task_worker->running = false;
            task_worker->stats.backend_available = false;
            if (failed_task != nullptr) {
                failed_task->executor = std::move(task_worker->executor);
                failed_task->engine = std::move(task_worker->engine);
            } else {
                task_worker->executor.reset();
                task_worker->engine.reset();
            }
            return false;
        }
        return true;
    }

    bool EnsureAlertExecutorLocked() {
        if (alert_executor) {
            return true;
        }
        std::shared_ptr<event::Executor> next_alert_executor(
            new event::Executor());
        event::ExecutorOptions alert_executor_options;
        alert_executor_options.worker_count = 1;
        alert_executor_options.queue_capacity = kAlertExecutorQueueCapacity;
        if (!next_alert_executor->Start(alert_executor_options)) {
            return false;
        }
        alert_executor = next_alert_executor;
        return true;
    }

    void MarkAllEnabledTaskBackendsUnavailableLocked() {
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            if (!task_worker || !task_worker->config.enabled) {
                continue;
            }
            task_worker->stats.enabled = true;
            task_worker->stats.backend_available = false;
        }
    }

    bool CanStartTaskWorkerLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker) const {
        if (!task_worker || !started || !config.enabled ||
            !task_worker->config.enabled) {
            return false;
        }
        for (const std::shared_ptr<AiTaskWorker> &current : task_workers) {
            if (current == task_worker) {
                return true;
            }
        }
        return false;
    }

    void MarkTaskBackendUnavailable(
        const std::shared_ptr<AiTaskWorker> &task_worker) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!CanStartTaskWorkerLocked(task_worker)) {
            return;
        }
        task_worker->running = false;
        task_worker->stats.enabled = true;
        task_worker->stats.backend_available = false;
    }

    bool CanTaskRunLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker) const {
        return task_worker && started && config.enabled &&
               task_worker->running && task_worker->config.enabled;
    }

    void MarkCaptureFailure(
        const std::shared_ptr<AiTaskWorker> &task_worker,
        const AiModelConfig &run_config) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!task_worker) {
            return;
        }
        ++task_worker->stats.skipped_frames;
        ++task_worker->stats.inference_failed_count;
        task_worker->stats.last_failure_time_ms =
            infra::Time::SystemTimeMillis();
        task_worker->last_result = AiInferenceResult{};
        task_worker->last_result.stream_id = run_config.stream_id;
        task_worker->last_result_time_ms = infra::Time::MonotonicMillis();
        task_worker->stats.active_results = 0;
    }

    void StopTaskWorkersLocked(
        std::vector<StoppedAiTask> *stopped_tasks) {
        if (stopped_tasks == nullptr) {
            return;
        }
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            if (!task_worker) {
                continue;
            }
            task_worker->running = false;
            task_worker->stats.backend_available = false;
            StoppedAiTask stopped;
            stopped.executor = std::move(task_worker->executor);
            stopped.engine = std::move(task_worker->engine);
            if (stopped.executor || stopped.engine) {
                stopped_tasks->push_back(std::move(stopped));
            }
        }
    }

    static void StopStoppedAiTasks(
        std::vector<StoppedAiTask> *stopped_tasks) {
        if (stopped_tasks == nullptr) {
            return;
        }
        for (StoppedAiTask &stopped : *stopped_tasks) {
            StopStoppedAiTask(&stopped);
        }
        stopped_tasks->clear();
    }

    static void StopStoppedAiTask(StoppedAiTask *stopped) {
        if (stopped == nullptr) {
            return;
        }
        if (stopped->executor) {
            stopped->executor->Stop(event::StopMode::kDiscard);
        }
        if (stopped->engine) {
            stopped->engine->Stop();
        }
        stopped->executor.reset();
        stopped->engine.reset();
    }

    static void StopAlertExecutor(
        std::shared_ptr<event::Executor> stopped_alert_executor) {
        if (stopped_alert_executor) {
            stopped_alert_executor->Stop(event::StopMode::kDiscard);
        }
    }

    static void StopAiTaskStartup(AiTaskStartup *startup) {
        if (startup == nullptr) {
            return;
        }
        if (startup->executor) {
            startup->executor->Stop(event::StopMode::kDiscard);
        }
        if (startup->engine) {
            startup->engine->Stop();
        }
        startup->executor.reset();
        startup->engine.reset();
    }

    void RebuildTaskWorkersLocked() {
        task_workers.clear();
        task_workers.reserve(config.tasks.size());
        for (const AiModelConfig &task_config : config.tasks) {
            std::shared_ptr<AiTaskWorker> task_worker(
                new AiTaskWorker(task_config));
            task_worker->stats.enabled = config.enabled && task_config.enabled;
            task_worker->stats.alarm_linked = options.alarm != nullptr;
            task_workers.push_back(task_worker);
        }
    }

    AiStats TaskStatsLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker) const {
        if (!task_worker) {
            return AiStats{};
        }
        AiStats stats = task_worker->stats;
        stats.enabled = config.enabled && task_worker->config.enabled;
        stats.backend_available =
            stats.enabled && task_worker->running && task_worker->engine &&
            task_worker->engine->Available();
        stats.alarm_linked = options.alarm != nullptr;
        stats.active_results =
            stats.enabled && IsAlertResultActive(task_worker->last_result)
                ? static_cast<uint32_t>(
                      task_worker->last_result.detections.size())
                : 0;
        return stats;
    }

    AiStats SummaryStatsLocked() const {
        AiStats summary;
        summary.enabled = config.enabled;
        summary.alarm_linked = options.alarm != nullptr;
        bool any_enabled_task = false;
        bool all_enabled_backends_available = true;
        uint64_t total_inference_time_ms = 0;
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            const AiStats task_stats = TaskStatsLocked(task_worker);
            if (task_worker && config.enabled && task_worker->config.enabled) {
                any_enabled_task = true;
                if (!task_stats.backend_available) {
                    all_enabled_backends_available = false;
                }
            }
            summary.last_success_time_ms =
                std::max(summary.last_success_time_ms,
                         task_stats.last_success_time_ms);
            summary.last_failure_time_ms =
                std::max(summary.last_failure_time_ms,
                         task_stats.last_failure_time_ms);
            summary.received_frames += task_stats.received_frames;
            summary.skipped_frames += task_stats.skipped_frames;
            summary.inference_count += task_stats.inference_count;
            summary.inference_failed_count +=
                task_stats.inference_failed_count;
            summary.dropped_tasks += task_stats.dropped_tasks;
            summary.last_inference_time_ms =
                std::max(summary.last_inference_time_ms,
                         task_stats.last_inference_time_ms);
            summary.max_inference_time_ms =
                std::max(summary.max_inference_time_ms,
                         task_stats.max_inference_time_ms);
            summary.active_results += task_stats.active_results;
            if (task_worker) {
                total_inference_time_ms +=
                    task_worker->inference_time_total_ms;
            }
        }
        summary.backend_available =
            !config.enabled || !any_enabled_task ||
            all_enabled_backends_available;
        if (summary.inference_count != 0) {
            summary.average_inference_time_ms = static_cast<uint32_t>(
                total_inference_time_ms / summary.inference_count);
        }
        return summary;
    }

    AiStats SnapshotSummaryStats() const {
        std::lock_guard<std::mutex> lock(mutex);
        return SummaryStatsLocked();
    }

    void UpdateInferenceTimeStatsLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker,
        int64_t inference_time_ms) {
        if (!task_worker) {
            return;
        }
        const uint32_t clamped_time_ms =
            ClampInferenceTime(inference_time_ms);
        task_worker->stats.last_inference_time_ms = clamped_time_ms;
        task_worker->stats.max_inference_time_ms =
            std::max(task_worker->stats.max_inference_time_ms,
                     clamped_time_ms);
        task_worker->inference_time_total_ms += clamped_time_ms;
        if (task_worker->stats.inference_count != 0) {
            task_worker->stats.average_inference_time_ms =
                static_cast<uint32_t>(
                    task_worker->inference_time_total_ms /
                    task_worker->stats.inference_count);
        }
    }

    AlarmInput AlarmInputLocked() const {
        AlarmInput input;
        input.source = AlarmSource::kAiDetection;
        int64_t latest_active_result_time_ms = 0;
        AiTask latest_active_task = AiTask::kObjectDetection;
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            if (!task_worker ||
                !IsAlertResultActive(task_worker->last_result)) {
                continue;
            }
            input.active = true;
            input.value +=
                static_cast<int32_t>(task_worker->last_result.detections.size());
            if (task_worker->last_result_time_ms >=
                latest_active_result_time_ms) {
                latest_active_result_time_ms =
                    task_worker->last_result_time_ms;
                latest_active_task = task_worker->config.task;
            }
        }
        if (input.active) {
            input.message =
                std::string("ai_") + TaskAlarmName(latest_active_task);
        }
        return input;
    }

    void PublishAlarmInputForState(
        const std::shared_ptr<AiTaskWorker> &task_worker) {
        if (options.alarm == nullptr) {
            return;
        }
        AlarmInput input;
        {
            std::lock_guard<std::mutex> lock(mutex);
            input = AlarmInputLocked();
        }
        if (!options.alarm->InjectAlarmInput(input)) {
            IncrementDroppedTasks(task_worker);
        }
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

    void IncrementDroppedTasks(
        const std::shared_ptr<AiTaskWorker> &task_worker) {
        std::lock_guard<std::mutex> lock(mutex);
        if (task_worker) {
            ++task_worker->stats.dropped_tasks;
        }
    }

    void AddAlert(const AiAlertRecord &alert) {
        std::vector<std::string> expired_image_paths;
        {
            std::lock_guard<std::mutex> lock(mutex);
            alerts.push_back(alert);
            while (alerts.size() > options.max_alert_records) {
                expired_image_paths.push_back(AlertImagePath(
                    options.alert_image_dir, alerts.front().id));
                alerts.erase(alerts.begin());
            }
        }
        for (const std::string &path : expired_image_paths) {
            static_cast<void>(infra::File::Remove(path));
        }
    }

    uint64_t NextAlertId() {
        std::lock_guard<std::mutex> lock(mutex);
        return next_alert_id++;
    }

    AiOptions options;
    AiConfig config;
    std::vector<std::shared_ptr<AiTaskWorker>> task_workers;
    std::shared_ptr<event::Executor> alert_executor;
    std::vector<AiAlertRecord> alerts;
    uint64_t next_alert_id = 1;
    bool config_attached = false;
    bool started = false;
    mutable std::mutex mutex;
    std::mutex capture_mutex;
};

AiCore::AiCore(const AiOptions &options) : state_(new State(options)) {}

AiCore::~AiCore() {
    if (state_) {
        state_->Release();
    }
}

bool AiCore::Start() { return state_ != nullptr && state_->Start(); }

void AiCore::Stop() {
    if (state_) {
        state_->Stop();
    }
}

AiCapabilities AiCore::GetCapabilities() const {
    return BuildAiCapabilities();
}

AiConfig AiCore::GetConfig() const {
    if (!state_) {
        return AiConfig{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->config;
}

AiStats AiCore::GetStats() const {
    if (!state_) {
        return AiStats{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->SummaryStatsLocked();
}

AiInferenceResult AiCore::GetLastResult() const {
    if (!state_) {
        return AiInferenceResult{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    AiInferenceResult latest_result;
    int64_t latest_result_time_ms = 0;
    for (const std::shared_ptr<AiTaskWorker> &task_worker :
         state_->task_workers) {
        if (!task_worker || !task_worker->last_result.success) {
            continue;
        }
        if (task_worker->last_result_time_ms >= latest_result_time_ms) {
            latest_result_time_ms = task_worker->last_result_time_ms;
            latest_result = task_worker->last_result;
        }
    }
    return latest_result;
}

std::vector<AiTaskStatus> AiCore::GetTaskStatuses() const {
    if (!state_) {
        return std::vector<AiTaskStatus>();
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<AiTaskStatus> statuses;
    statuses.reserve(state_->task_workers.size());
    for (const std::shared_ptr<AiTaskWorker> &task_worker :
         state_->task_workers) {
        if (!task_worker) {
            continue;
        }
        AiTaskStatus status;
        status.config = task_worker->config;
        status.stats = state_->TaskStatsLocked(task_worker);
        status.last_result = task_worker->last_result;
        statuses.push_back(status);
    }
    return statuses;
}

std::vector<AiAlertRecord> AiCore::ListAlerts() const {
    if (!state_) {
        return std::vector<AiAlertRecord>();
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<AiAlertRecord> alerts = state_->alerts;
    std::reverse(alerts.begin(), alerts.end());
    return alerts;
}

std::string AiCore::ReadAlertImage(const std::string &id) const {
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
