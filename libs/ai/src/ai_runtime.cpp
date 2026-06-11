#include "ai_runtime.h"

#include "ai_config.h"
#include "ai_engine.h"
#include "alarm.h"
#include "config.h"
#include "device.h"
#include "infra/executor.h"
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

using ai_internal::AiBackendName;
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
    config.input_width = 300;
    config.input_height = 300;
    config.inference_interval_ms = 500;
    config.max_results = 16;
    config.confidence_threshold = 0.5f;
    if (task == AiTask::kObjectDetection ||
        task == AiTask::kPerimeterDetection) {
        config.model_path = "models/inst_ssd_cycle.wk";
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

}  // namespace

struct AiTaskRuntime final {
    explicit AiTaskRuntime(const AiModelConfig &task_config)
        : config(task_config) {}

    AiModelConfig config;
    std::shared_ptr<AiInferenceEngine> engine;
    std::unique_ptr<infra::Executor> executor;
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
    std::shared_ptr<AiTaskRuntime> task_runtime;
    int64_t timestamp_ms = 0;
};

struct StoppedTaskRuntime {
    std::unique_ptr<infra::Executor> executor;
    std::shared_ptr<AiInferenceEngine> engine;
};

struct TaskRuntimeStartup {
    std::shared_ptr<AiTaskRuntime> task_runtime;
    std::shared_ptr<AiInferenceEngine> engine;
    std::unique_ptr<infra::Executor> executor;
};

struct AiRuntime::State final {
    explicit State(const AiOptions &service_options)
        : options(service_options), config(service_options.default_config) {
        if (options.max_alert_records == 0) {
            options.max_alert_records = 100;
        }
        if (!IsValidAiConfig(config)) {
            config = DefaultAiConfig();
        }
        RebuildTaskRuntimesLocked();
    }

    bool Prepare() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!IsValidAiConfig(config)) {
                return false;
            }
            if (options.config != nullptr && !config_attached) {
                ConfigAttachment attachment;
                attachment.validate = [this](const ConfigJson &value) {
                    AiConfig current_config;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        current_config = config;
                    }
                    AiConfig parsed;
                    return ParseAiConfig(value, current_config, &parsed)
                               ? ConfigResult::Success()
                               : ConfigResult::Failure(
                                     "", "invalid ai config");
                };
                attachment.apply = [this](const ConfigJson &value) {
                    AiConfig current_config;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        current_config = config;
                    }
                    AiConfig parsed;
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
        }

        if (options.config != nullptr) {
            ConfigJson ai_config = options.config->GetValue("ai");
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
                RebuildTaskRuntimesLocked();
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
            RebuildTaskRuntimesLocked();
        }

        StartConfiguredTaskRuntimes();

        AiStats summary = SnapshotSummaryStats();
        std::size_t configured_task_count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            configured_task_count = task_runtimes.size();
        }
        Info("ai", "AI started: enabled=%d tasks=%u",
             summary.enabled ? 1 : 0,
             static_cast<unsigned int>(configured_task_count));
        return true;
    }

    void Stop() {
        std::vector<StoppedTaskRuntime> stopped_tasks;
        std::shared_ptr<infra::Executor> stopped_alert_executor;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started && task_runtimes.empty() && !alert_executor) {
                return;
            }
            started = false;
            StopTaskRuntimesLocked(&stopped_tasks);
            stopped_alert_executor = std::move(alert_executor);
        }
        StopStoppedTaskRuntimes(&stopped_tasks);
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
            static_cast<void>(options.config->DetachConfig("ai"));
        }
    }

    void CaptureLoop(std::shared_ptr<AiTaskRuntime> task_runtime) {
        int64_t next_inference_ms = infra::Time::MonotonicMillis();
        while (true) {
            AiModelConfig run_config;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!CanTaskRunLocked(task_runtime)) {
                    return;
                }
                run_config = task_runtime->config;
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
                    if (!CanTaskRunLocked(task_runtime)) {
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
                MarkCaptureFailure(task_runtime, run_config);
                PublishAlarmInputForState(task_runtime);
                continue;
            }

            PendingAlertCapture pending_alert;
            if (RunInference(frame, task_runtime, run_config,
                             &pending_alert)) {
                PostAlertCapture(pending_alert);
            }
        }
    }

    bool RunInference(const hisisdk::YuvFrame &frame,
                      const std::shared_ptr<AiTaskRuntime> &task_runtime,
                      const AiModelConfig &run_config,
                      PendingAlertCapture *pending_alert) {
        std::shared_ptr<AiInferenceEngine> run_engine;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!CanTaskRunLocked(task_runtime) || !task_runtime->engine) {
                ++task_runtime->stats.inference_failed_count;
                task_runtime->stats.last_failure_time_ms =
                    infra::Time::SystemTimeMillis();
                return false;
            }
            ++task_runtime->stats.received_frames;
            run_engine = task_runtime->engine;
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
            if (!CanTaskRunLocked(task_runtime)) {
                return false;
            }
            if (result.success) {
                ++task_runtime->stats.inference_count;
                task_runtime->stats.last_success_time_ms =
                    infra::Time::SystemTimeMillis();
                UpdateInferenceTimeStatsLocked(task_runtime,
                                               inference_time_ms);
            } else {
                ++task_runtime->stats.inference_failed_count;
                task_runtime->stats.last_failure_time_ms =
                    infra::Time::SystemTimeMillis();
            }
            task_runtime->last_result = result;
            task_runtime->last_result_time_ms =
                infra::Time::MonotonicMillis();
            task_runtime->stats.active_results =
                HasAlertDetections(result)
                    ? static_cast<uint32_t>(result.detections.size())
                    : 0;
        }

        PublishAlarmInputForState(task_runtime);
        return PrepareAlertCapture(result, run_config, task_runtime,
                                   pending_alert);
    }

    bool PrepareAlertCapture(
        const AiInferenceResult &result,
        const AiModelConfig &run_config,
        const std::shared_ptr<AiTaskRuntime> &task_runtime,
        PendingAlertCapture *pending_alert) {
        if (!HasAlertDetections(result) || options.device == nullptr ||
            pending_alert == nullptr) {
            return false;
        }
        const int64_t now_ms = infra::Time::SystemTimeMillis();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!CanTaskRunLocked(task_runtime) ||
                now_ms - task_runtime->last_alert_ms < kMinAlertIntervalMs) {
                return false;
            }
            task_runtime->last_alert_ms = now_ms;
        }
        pending_alert->result = result;
        pending_alert->config = run_config;
        pending_alert->task_runtime = task_runtime;
        pending_alert->timestamp_ms = now_ms;
        return true;
    }

    void PostAlertCapture(PendingAlertCapture pending_alert) {
        std::shared_ptr<infra::Executor> executor_snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !alert_executor) {
                return;
            }
            executor_snapshot = alert_executor;
        }
        if (!executor_snapshot->Post([this, pending_alert]() {
                SaveAlertCapture(pending_alert);
            })) {
            IncrementDroppedTasks(pending_alert.task_runtime);
        }
    }

    void SaveAlertCapture(const PendingAlertCapture &pending_alert) {
        SnapshotRequest request;
        request.stream_id = pending_alert.config.stream_id;
        request.include_thumbnail = false;
        SnapshotFrame frame = options.device->CaptureSnapshot(request);
        if (!LooksLikeJpeg(frame)) {
            IncrementDroppedTasks(pending_alert.task_runtime);
            return;
        }

        if (!infra::Path::MakeDirs(options.alert_image_dir)) {
            IncrementDroppedTasks(pending_alert.task_runtime);
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
            IncrementDroppedTasks(pending_alert.task_runtime);
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
        std::vector<StoppedTaskRuntime> stopped_tasks;
        std::shared_ptr<infra::Executor> stopped_alert_executor;
        {
            std::lock_guard<std::mutex> lock(mutex);
            service_started = started;
            StopTaskRuntimesLocked(&stopped_tasks);
            stopped_alert_executor = std::move(alert_executor);
            config = next_config;
            RebuildTaskRuntimesLocked();
        }
        StopStoppedTaskRuntimes(&stopped_tasks);
        StopAlertExecutor(stopped_alert_executor);

        if (!service_started) {
            return true;
        }

        StartConfiguredTaskRuntimes();
        PublishAlarmInputForState(nullptr);
        Info("ai", "AI config applied: enabled=%d tasks=%u",
             next_config.enabled ? 1 : 0,
             static_cast<unsigned int>(next_config.tasks.size()));
        if (!next_config.enabled) {
            ClearAlarmInput();
        }
        return true;
    }

    void StartConfiguredTaskRuntimes() {
        std::vector<std::shared_ptr<AiTaskRuntime>> enabled_task_runtimes;
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
            for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
                 task_runtimes) {
                if (!task_runtime->config.enabled) {
                    task_runtime->stats.enabled = false;
                    continue;
                }
                task_runtime->stats.enabled = true;
                task_runtime->stats.backend_available = false;
                enabled_task_runtimes.push_back(task_runtime);
            }
        }

        Info("ai", "AI enabled task startup count=%u",
             static_cast<unsigned int>(enabled_task_runtimes.size()));
        for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
             enabled_task_runtimes) {
            TaskRuntimeStartup startup;
            if (!BuildTaskRuntimeStartup(task_runtime, &startup)) {
                MarkTaskBackendUnavailable(task_runtime);
                continue;
            }

            StoppedTaskRuntime failed_runtime;
            if (!CommitTaskRuntimeStartup(&startup, &failed_runtime)) {
                StopTaskRuntimeStartup(&startup);
                StopStoppedTaskRuntime(&failed_runtime);
            }
        }
    }

    bool BuildTaskRuntimeStartup(
        const std::shared_ptr<AiTaskRuntime> &task_runtime,
        TaskRuntimeStartup *startup) {
        if (!task_runtime || startup == nullptr) {
            return false;
        }
        const AiModelConfig &task_config = task_runtime->config;
        Info("ai",
             "AI backend start begin: backend=%s task=%d model=%s stream=%d "
             "input=%ux%u interval=%u threshold=%.3f max=%u",
             AiBackendName(task_config.backend),
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
                  AiBackendName(task_config.backend),
                  static_cast<int>(task_config.task));
            return false;
        }
        Info("ai", "AI backend created: backend=%s task=%d available=%d",
             AiBackendName(task_config.backend),
             static_cast<int>(task_config.task),
             next_engine->Available() ? 1 : 0);
        if (!next_engine->Available() || !next_engine->Start(task_config)) {
            Error("ai", "Start AI backend failed: backend=%s task=%d model=%s",
                  AiBackendName(task_config.backend),
                  static_cast<int>(task_config.task),
                  task_config.model_path.c_str());
            next_engine->Stop();
            return false;
        }
        Info("ai", "AI backend start done: backend=%s task=%d",
             AiBackendName(task_config.backend),
             static_cast<int>(task_config.task));

        std::unique_ptr<infra::Executor> next_executor(new infra::Executor());
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = 1;
        executor_options.queue_capacity = kDefaultExecutorQueueCapacity;
        if (!next_executor->Start(executor_options)) {
            next_engine->Stop();
            return false;
        }

        startup->task_runtime = task_runtime;
        startup->engine = next_engine;
        startup->executor = std::move(next_executor);
        return true;
    }

    bool CommitTaskRuntimeStartup(TaskRuntimeStartup *startup,
                                  StoppedTaskRuntime *failed_runtime) {
        if (startup == nullptr || !startup->task_runtime || !startup->engine ||
            !startup->executor) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex);
        std::shared_ptr<AiTaskRuntime> task_runtime = startup->task_runtime;
        if (!CanStartTaskRuntimeLocked(task_runtime)) {
            return false;
        }

        task_runtime->engine = startup->engine;
        task_runtime->executor = std::move(startup->executor);
        task_runtime->stats.enabled = true;
        task_runtime->stats.backend_available = startup->engine->Available();
        task_runtime->running = true;
        startup->engine.reset();
        if (!task_runtime->executor->Post([this, task_runtime]() {
                CaptureLoop(task_runtime);
            })) {
            task_runtime->running = false;
            task_runtime->stats.backend_available = false;
            if (failed_runtime != nullptr) {
                failed_runtime->executor = std::move(task_runtime->executor);
                failed_runtime->engine = std::move(task_runtime->engine);
            } else {
                task_runtime->executor.reset();
                task_runtime->engine.reset();
            }
            return false;
        }
        return true;
    }

    bool EnsureAlertExecutorLocked() {
        if (alert_executor) {
            return true;
        }
        std::shared_ptr<infra::Executor> next_alert_executor(
            new infra::Executor());
        infra::ExecutorOptions alert_executor_options;
        alert_executor_options.worker_count = 1;
        alert_executor_options.queue_capacity = kAlertExecutorQueueCapacity;
        if (!next_alert_executor->Start(alert_executor_options)) {
            return false;
        }
        alert_executor = next_alert_executor;
        return true;
    }

    void MarkAllEnabledTaskBackendsUnavailableLocked() {
        for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
             task_runtimes) {
            if (!task_runtime || !task_runtime->config.enabled) {
                continue;
            }
            task_runtime->stats.enabled = true;
            task_runtime->stats.backend_available = false;
        }
    }

    bool CanStartTaskRuntimeLocked(
        const std::shared_ptr<AiTaskRuntime> &task_runtime) const {
        if (!task_runtime || !started || !config.enabled ||
            !task_runtime->config.enabled) {
            return false;
        }
        for (const std::shared_ptr<AiTaskRuntime> &current : task_runtimes) {
            if (current == task_runtime) {
                return true;
            }
        }
        return false;
    }

    void MarkTaskBackendUnavailable(
        const std::shared_ptr<AiTaskRuntime> &task_runtime) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!CanStartTaskRuntimeLocked(task_runtime)) {
            return;
        }
        task_runtime->running = false;
        task_runtime->stats.enabled = true;
        task_runtime->stats.backend_available = false;
    }

    bool CanTaskRunLocked(
        const std::shared_ptr<AiTaskRuntime> &task_runtime) const {
        return task_runtime && started && config.enabled &&
               task_runtime->running && task_runtime->config.enabled;
    }

    void MarkCaptureFailure(
        const std::shared_ptr<AiTaskRuntime> &task_runtime,
        const AiModelConfig &run_config) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!task_runtime) {
            return;
        }
        ++task_runtime->stats.skipped_frames;
        ++task_runtime->stats.inference_failed_count;
        task_runtime->stats.last_failure_time_ms =
            infra::Time::SystemTimeMillis();
        task_runtime->last_result = AiInferenceResult{};
        task_runtime->last_result.stream_id = run_config.stream_id;
        task_runtime->last_result_time_ms = infra::Time::MonotonicMillis();
        task_runtime->stats.active_results = 0;
    }

    void StopTaskRuntimesLocked(
        std::vector<StoppedTaskRuntime> *stopped_tasks) {
        if (stopped_tasks == nullptr) {
            return;
        }
        for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
             task_runtimes) {
            if (!task_runtime) {
                continue;
            }
            task_runtime->running = false;
            task_runtime->stats.backend_available = false;
            StoppedTaskRuntime stopped;
            stopped.executor = std::move(task_runtime->executor);
            stopped.engine = std::move(task_runtime->engine);
            if (stopped.executor || stopped.engine) {
                stopped_tasks->push_back(std::move(stopped));
            }
        }
    }

    static void StopStoppedTaskRuntimes(
        std::vector<StoppedTaskRuntime> *stopped_tasks) {
        if (stopped_tasks == nullptr) {
            return;
        }
        for (StoppedTaskRuntime &stopped : *stopped_tasks) {
            StopStoppedTaskRuntime(&stopped);
        }
        stopped_tasks->clear();
    }

    static void StopStoppedTaskRuntime(StoppedTaskRuntime *stopped) {
        if (stopped == nullptr) {
            return;
        }
        if (stopped->executor) {
            stopped->executor->Stop(infra::StopMode::kDiscard);
        }
        if (stopped->engine) {
            stopped->engine->Stop();
        }
        stopped->executor.reset();
        stopped->engine.reset();
    }

    static void StopAlertExecutor(
        std::shared_ptr<infra::Executor> stopped_alert_executor) {
        if (stopped_alert_executor) {
            stopped_alert_executor->Stop(infra::StopMode::kDiscard);
        }
    }

    static void StopTaskRuntimeStartup(TaskRuntimeStartup *startup) {
        if (startup == nullptr) {
            return;
        }
        if (startup->executor) {
            startup->executor->Stop(infra::StopMode::kDiscard);
        }
        if (startup->engine) {
            startup->engine->Stop();
        }
        startup->executor.reset();
        startup->engine.reset();
    }

    void RebuildTaskRuntimesLocked() {
        task_runtimes.clear();
        task_runtimes.reserve(config.tasks.size());
        for (const AiModelConfig &task_config : config.tasks) {
            std::shared_ptr<AiTaskRuntime> task_runtime(
                new AiTaskRuntime(task_config));
            task_runtime->stats.enabled = config.enabled && task_config.enabled;
            task_runtime->stats.alarm_linked = options.alarm != nullptr;
            task_runtimes.push_back(task_runtime);
        }
    }

    AiStats TaskStatsLocked(
        const std::shared_ptr<AiTaskRuntime> &task_runtime) const {
        if (!task_runtime) {
            return AiStats{};
        }
        AiStats stats = task_runtime->stats;
        stats.enabled = config.enabled && task_runtime->config.enabled;
        stats.backend_available =
            stats.enabled && task_runtime->running && task_runtime->engine &&
            task_runtime->engine->Available();
        stats.alarm_linked = options.alarm != nullptr;
        stats.active_results =
            stats.enabled && HasAlertDetections(task_runtime->last_result)
                ? static_cast<uint32_t>(
                      task_runtime->last_result.detections.size())
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
        for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
             task_runtimes) {
            const AiStats task_stats = TaskStatsLocked(task_runtime);
            if (task_runtime && config.enabled && task_runtime->config.enabled) {
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
            if (task_runtime) {
                total_inference_time_ms +=
                    task_runtime->inference_time_total_ms;
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
        const std::shared_ptr<AiTaskRuntime> &task_runtime,
        int64_t inference_time_ms) {
        if (!task_runtime) {
            return;
        }
        const uint32_t clamped_time_ms =
            ClampInferenceTime(inference_time_ms);
        task_runtime->stats.last_inference_time_ms = clamped_time_ms;
        task_runtime->stats.max_inference_time_ms =
            std::max(task_runtime->stats.max_inference_time_ms,
                     clamped_time_ms);
        task_runtime->inference_time_total_ms += clamped_time_ms;
        if (task_runtime->stats.inference_count != 0) {
            task_runtime->stats.average_inference_time_ms =
                static_cast<uint32_t>(
                    task_runtime->inference_time_total_ms /
                    task_runtime->stats.inference_count);
        }
    }

    AlarmInput AlarmInputLocked() const {
        AlarmInput input;
        input.source = AlarmSource::kAiDetection;
        int64_t latest_active_result_time_ms = 0;
        AiTask latest_active_task = AiTask::kObjectDetection;
        for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
             task_runtimes) {
            if (!task_runtime || !HasAlertDetections(task_runtime->last_result)) {
                continue;
            }
            input.active = true;
            input.value +=
                static_cast<int32_t>(task_runtime->last_result.detections.size());
            if (task_runtime->last_result_time_ms >=
                latest_active_result_time_ms) {
                latest_active_result_time_ms =
                    task_runtime->last_result_time_ms;
                latest_active_task = task_runtime->config.task;
            }
        }
        if (input.active) {
            input.message =
                std::string("ai_") + TaskAlarmName(latest_active_task);
        }
        return input;
    }

    void PublishAlarmInputForState(
        const std::shared_ptr<AiTaskRuntime> &task_runtime) {
        if (options.alarm == nullptr) {
            return;
        }
        AlarmInput input;
        {
            std::lock_guard<std::mutex> lock(mutex);
            input = AlarmInputLocked();
        }
        if (!options.alarm->InjectAlarmInput(input)) {
            IncrementDroppedTasks(task_runtime);
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
        const std::shared_ptr<AiTaskRuntime> &task_runtime) {
        std::lock_guard<std::mutex> lock(mutex);
        if (task_runtime) {
            ++task_runtime->stats.dropped_tasks;
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
    std::vector<std::shared_ptr<AiTaskRuntime>> task_runtimes;
    std::shared_ptr<infra::Executor> alert_executor;
    std::vector<AiAlertRecord> alerts;
    uint64_t next_alert_id = 1;
    bool config_attached = false;
    bool started = false;
    mutable std::mutex mutex;
    std::mutex capture_mutex;
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

AiConfig AiRuntime::GetConfig() const {
    if (!state_) {
        return AiConfig{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->config;
}

AiStats AiRuntime::GetStats() const {
    if (!state_) {
        return AiStats{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->SummaryStatsLocked();
}

AiInferenceResult AiRuntime::GetLastResult() const {
    if (!state_) {
        return AiInferenceResult{};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    AiInferenceResult latest_result;
    int64_t latest_result_time_ms = 0;
    for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
         state_->task_runtimes) {
        if (!task_runtime || !task_runtime->last_result.success) {
            continue;
        }
        if (task_runtime->last_result_time_ms >= latest_result_time_ms) {
            latest_result_time_ms = task_runtime->last_result_time_ms;
            latest_result = task_runtime->last_result;
        }
    }
    return latest_result;
}

std::vector<AiTaskStatus> AiRuntime::GetTaskStatuses() const {
    if (!state_) {
        return std::vector<AiTaskStatus>();
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<AiTaskStatus> statuses;
    statuses.reserve(state_->task_runtimes.size());
    for (const std::shared_ptr<AiTaskRuntime> &task_runtime :
         state_->task_runtimes) {
        if (!task_runtime) {
            continue;
        }
        AiTaskStatus status;
        status.config = task_runtime->config;
        status.stats = state_->TaskStatsLocked(task_runtime);
        status.last_result = task_runtime->last_result;
        statuses.push_back(status);
    }
    return statuses;
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
