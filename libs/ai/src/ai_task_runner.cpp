#include "ai_task_runner.h"

#include "ai_alert_images.h"
#include "ai_backend_runner.h"
#include "ai_config.h"
#include "ai_defaults.h"
#include "ai_frame_capture.h"
#include "alarm.h"
#include "config.h"
#include "device.h"
#include "hisi_ai_platform.h"
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
constexpr uint32_t kCaptureStopPollMs = 50;
constexpr int64_t kMinAlertIntervalMs = 1000;

using ai_internal::AiBackendRunner;
using ai_internal::AiBackendToString;
using ai_internal::AiAlertCapture;
using ai_internal::AiAlertImages;
using ai_internal::AiFrameCapture;
using ai_internal::BuildAiCapabilities;
using ai_internal::CreateAiBackendRunner;
using ai_internal::DefaultAiConfig;
using ai_internal::FilterPerimeterDetections;
using ai_internal::IsValidAiConfig;
using ai_internal::ParseAiConfig;

bool IsAlertResultActive(const AiInferenceResult &result) {
    return result.success && !result.detections.empty();
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

uint32_t ClampInferenceTime(int64_t inference_time_ms) {
    if (inference_time_ms <= 0) {
        return 0;
    }
    return static_cast<uint32_t>(std::min<int64_t>(
        inference_time_ms,
        static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
}

bool HasTaskConfig(const std::vector<AiModelConfig> &tasks, AiTask task) {
    for (const AiModelConfig &task_config : tasks) {
        if (task_config.task == task) {
            return true;
        }
    }
    return false;
}

bool SamePerimeterConfig(const AiPerimeterConfig &lhs,
                         const AiPerimeterConfig &rhs) {
    if (lhs.regions.size() != rhs.regions.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.regions.size(); ++i) {
        const AiPerimeterRegion &left = lhs.regions[i];
        const AiPerimeterRegion &right = rhs.regions[i];
        if (left.name != right.name || left.x != right.x ||
            left.y != right.y || left.width != right.width ||
            left.height != right.height) {
            return false;
        }
    }
    return true;
}

bool SameTaskConfig(const AiModelConfig &lhs, const AiModelConfig &rhs) {
    return lhs.enabled == rhs.enabled && lhs.backend == rhs.backend &&
           lhs.task == rhs.task && lhs.stream_id == rhs.stream_id &&
           lhs.model_path == rhs.model_path &&
           lhs.input_width == rhs.input_width &&
           lhs.input_height == rhs.input_height &&
           lhs.inference_interval_ms == rhs.inference_interval_ms &&
           lhs.max_results == rhs.max_results &&
           lhs.confidence_threshold == rhs.confidence_threshold &&
           SamePerimeterConfig(lhs.perimeter, rhs.perimeter);
}

}  // namespace

struct AiTaskWorker final {
    explicit AiTaskWorker(const AiModelConfig &task_config)
        : config(task_config) {}

    AiModelConfig config;
    std::shared_ptr<AiBackendRunner> backend_runner;
    std::unique_ptr<event::Executor> executor;
    AiInferenceResult last_result;
    AiStats stats;
    uint64_t inference_time_total_ms = 0;
    int64_t last_result_time_ms = 0;
    int64_t last_alert_ms = 0;
    bool running = false;
};

struct StoppedAiTask {
    std::unique_ptr<event::Executor> executor;
    std::shared_ptr<AiBackendRunner> backend_runner;
};

struct AiTaskStartup {
    std::shared_ptr<AiTaskWorker> task_worker;
    std::shared_ptr<AiBackendRunner> backend_runner;
    std::unique_ptr<event::Executor> executor;
};

struct AiTaskRunner::State final {
    explicit State(const AiOptions &service_options)
        : options(service_options),
          config(service_options.default_config),
          frame_capture(service_options.snapshot,
                        service_options.media_channels),
          alert_images(service_options.alert_image_dir,
                       service_options.max_alert_records) {
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
                config_scope.verify = [this](const Json &now,
                                             ConfigError *error) {
                    AiConfig current_config;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        current_config = config;
                    }
                    AiConfig parsed;
                    if (ParseAiConfig(now, current_config, &parsed)) {
                        return ConfigCode::kOk;
                    }
                    if (error != nullptr) {
                        error->field.clear();
                        error->message = "invalid ai config";
                    }
                    return ConfigCode::kVerify;
                };
                config_scope.apply = [this](const Json &prev,
                                            const Json &now,
                                            ConfigError *error) {
                    (void)prev;
                    AiConfig current_config;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        current_config = config;
                    }
                    AiConfig parsed;
                    if (!ParseAiConfig(now, current_config, &parsed)) {
                        if (error != nullptr) {
                            error->field.clear();
                            error->message = "invalid ai config";
                        }
                        return ConfigCode::kVerify;
                    }
                    if (ApplyConfig(parsed)) {
                        return ConfigCode::kOk;
                    }
                    if (error != nullptr) {
                        error->field.clear();
                        error->message = "apply ai config failed";
                    }
                    return ConfigCode::kApply;
                };
                if (!options.config->AddScope("ai", config_scope)) {
                    return false;
                }
                config_attached = true;
            }
        }

        if (options.config != nullptr) {
            Json ai_config = options.config->Get("ai");
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
        std::size_t configured_task_size = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            configured_task_size = task_workers.size();
        }
        Info("ai", "AI started: enabled=%d tasks=%u",
             summary.enabled ? 1 : 0,
             static_cast<unsigned int>(configured_task_size));
        return true;
    }

    void Stop() {
        std::vector<StoppedAiTask> stopped_tasks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started && task_workers.empty()) {
                return;
            }
            started = false;
            StopTaskWorkersLocked(stopped_tasks);
        }
        StopStoppedAiTasks(stopped_tasks);
        alert_images.Stop();
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
                std::lock_guard<std::mutex> lock(mutex);
                if (!CanTaskRunLocked(task_worker)) {
                    return;
                }
            }
            frame = frame_capture.Capture(run_config);

            if (!frame.Valid()) {
                MarkCaptureFailure(task_worker, run_config);
                PublishAlarmInputForState(task_worker);
                continue;
            }

            AiAlertCapture pending_alert;
            if (RunInference(frame, task_worker, run_config,
                             pending_alert)) {
                PostAlertCapture(pending_alert);
            }
        }
    }

    bool RunInference(const hisisdk::YuvFrame &frame,
                      const std::shared_ptr<AiTaskWorker> &task_worker,
                      const AiModelConfig &run_config,
                      AiAlertCapture &pending_alert) {
        std::shared_ptr<AiBackendRunner> run_backend_runner;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!CanTaskRunLocked(task_worker) || !task_worker->backend_runner) {
                ++task_worker->stats.failed_inferences;
                task_worker->stats.last_failure_time_ms =
                    infra::Time::SystemTimeMillis();
                return false;
            }
            ++task_worker->stats.received_frames;
            run_backend_runner = task_worker->backend_runner;
        }

        const int64_t inference_start_ms = infra::Time::MonotonicMillis();
        AiInferenceResult result =
            run_backend_runner->Run(frame, run_config.stream_id, run_config);
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
                ++task_worker->stats.inferences;
                task_worker->stats.last_success_time_ms =
                    infra::Time::SystemTimeMillis();
                UpdateInferenceTimeStatsLocked(task_worker,
                                               inference_time_ms);
            } else {
                ++task_worker->stats.failed_inferences;
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
        AiAlertCapture &pending_alert) {
        if (!IsAlertResultActive(result) || options.device == nullptr) {
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
        pending_alert.result = result;
        pending_alert.config = run_config;
        pending_alert.timestamp_ms = now_ms;
        pending_alert.on_drop = [this, task_worker]() {
            IncrementDroppedTasks(task_worker);
        };
        return true;
    }

    void PostAlertCapture(const AiAlertCapture &pending_alert) {
        bool service_started = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            service_started = started;
        }
        if (!service_started) {
            return;
        }
        static_cast<void>(alert_images.PostCapture(options.device,
                                                   pending_alert));
    }

    bool ApplyConfig(const AiConfig &next_config) {
        if (!IsValidAiConfig(next_config)) {
            return false;
        }

        uint32_t enabled_task_size = 0;
        for (const AiModelConfig &task_config : next_config.tasks) {
            if (task_config.enabled) {
                ++enabled_task_size;
            }
        }
        Info("ai", "AI config apply begin: enabled=%d tasks=%u enabled=%u",
             next_config.enabled ? 1 : 0,
             static_cast<unsigned int>(next_config.tasks.size()),
             static_cast<unsigned int>(enabled_task_size));

        bool service_started = false;
        bool service_enabled = false;
        std::vector<StoppedAiTask> stopped_tasks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            service_started = started;
            service_enabled = next_config.enabled;
            ApplyTaskConfigDiffLocked(next_config, stopped_tasks);
            config = next_config;
        }
        const bool task_workers_stopped = !stopped_tasks.empty();
        if (!service_enabled || task_workers_stopped) {
            alert_images.Stop();
        }
        StopStoppedAiTasks(stopped_tasks);

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
            if (options.device == nullptr || !frame_capture.Available() ||
                !options.device->IsStarted()) {
                Error("ai", "AI startup skipped: device media or sdk unavailable");
                MarkAllEnabledTaskBackendsUnavailableLocked();
                return;
            }
            if (!alert_images.Start()) {
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
                if (task_worker->running) {
                    continue;
                }
                task_worker->stats.backend_available = false;
                enabled_task_workers.push_back(task_worker);
            }
        }

        Info("ai", "AI enabled task startup size=%u",
             static_cast<unsigned int>(enabled_task_workers.size()));
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             enabled_task_workers) {
            AiTaskStartup startup;
            if (!BuildAiTaskStartup(task_worker, startup)) {
                MarkTaskBackendUnavailable(task_worker);
                continue;
            }

            StoppedAiTask failed_task;
            if (!CommitAiTaskStartup(startup, failed_task)) {
                StopAiTaskStartup(startup);
                StopStoppedAiTask(failed_task);
            }
        }
    }

    bool BuildAiTaskStartup(
        const std::shared_ptr<AiTaskWorker> &task_worker,
        AiTaskStartup &startup) {
        if (!task_worker) {
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
        std::shared_ptr<AiBackendRunner> next_backend_runner =
            CreateAiBackendRunner(task_config.backend);
        if (!next_backend_runner) {
            Error("ai", "Create AI backend failed: backend=%s task=%d",
                  AiBackendToString(task_config.backend),
                  static_cast<int>(task_config.task));
            return false;
        }
        Info("ai", "AI backend created: backend=%s task=%d available=%d",
             AiBackendToString(task_config.backend),
             static_cast<int>(task_config.task),
             next_backend_runner->Available() ? 1 : 0);
        if (!next_backend_runner->Available() || !next_backend_runner->Start(task_config)) {
            Error("ai", "Start AI backend failed: backend=%s task=%d model=%s",
                  AiBackendToString(task_config.backend),
                  static_cast<int>(task_config.task),
                  task_config.model_path.c_str());
            next_backend_runner->Stop();
            return false;
        }
        Info("ai", "AI backend start done: backend=%s task=%d",
             AiBackendToString(task_config.backend),
             static_cast<int>(task_config.task));

        std::unique_ptr<event::Executor> next_executor(new event::Executor());
        event::ExecutorOptions executor_options;
        executor_options.workers = 1;
        executor_options.queue_capacity = kDefaultExecutorQueueCapacity;
        if (!next_executor->Start(executor_options)) {
            next_backend_runner->Stop();
            return false;
        }

        startup.task_worker = task_worker;
        startup.backend_runner = next_backend_runner;
        startup.executor = std::move(next_executor);
        return true;
    }

    bool CommitAiTaskStartup(AiTaskStartup &startup,
                             StoppedAiTask &failed_task) {
        if (!startup.task_worker || !startup.backend_runner ||
            !startup.executor) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex);
        std::shared_ptr<AiTaskWorker> task_worker = startup.task_worker;
        if (!CanStartTaskWorkerLocked(task_worker)) {
            return false;
        }

        task_worker->backend_runner = startup.backend_runner;
        task_worker->executor = std::move(startup.executor);
        task_worker->stats.enabled = true;
        task_worker->stats.backend_available =
            startup.backend_runner->Available();
        task_worker->running = true;
        startup.backend_runner.reset();
        if (task_worker->executor->Post([this, task_worker]() {
                CaptureLoop(task_worker);
            }) != event::EventStatus::kOk) {
            task_worker->running = false;
            task_worker->stats.backend_available = false;
            failed_task.executor = std::move(task_worker->executor);
            failed_task.backend_runner =
                std::move(task_worker->backend_runner);
            return false;
        }
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
        ++task_worker->stats.failed_inferences;
        task_worker->stats.last_failure_time_ms =
            infra::Time::SystemTimeMillis();
        task_worker->last_result = AiInferenceResult{};
        task_worker->last_result.stream_id = run_config.stream_id;
        task_worker->last_result_time_ms = infra::Time::MonotonicMillis();
        task_worker->stats.active_results = 0;
    }

    void StopTaskWorkersLocked(std::vector<StoppedAiTask> &stopped_tasks) {
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            StopTaskWorkerLocked(task_worker, stopped_tasks);
        }
    }

    void StopTaskWorkerLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker,
        std::vector<StoppedAiTask> &stopped_tasks) {
        if (!task_worker) {
            return;
        }
        task_worker->running = false;
        task_worker->stats.backend_available = false;
        StoppedAiTask stopped;
        stopped.executor = std::move(task_worker->executor);
        stopped.backend_runner = std::move(task_worker->backend_runner);
        if (stopped.executor || stopped.backend_runner) {
            stopped_tasks.push_back(std::move(stopped));
        }
    }

    static void StopStoppedAiTasks(
        std::vector<StoppedAiTask> &stopped_tasks) {
        for (StoppedAiTask &stopped : stopped_tasks) {
            StopStoppedAiTask(stopped);
        }
        stopped_tasks.clear();
    }

    static void StopStoppedAiTask(StoppedAiTask &stopped) {
        if (stopped.executor) {
            stopped.executor->Stop(event::StopMode::kDiscard);
        }
        if (stopped.backend_runner) {
            stopped.backend_runner->Stop();
        }
        stopped.executor.reset();
        stopped.backend_runner.reset();
    }

    static void StopAiTaskStartup(AiTaskStartup &startup) {
        if (startup.executor) {
            startup.executor->Stop(event::StopMode::kDiscard);
        }
        if (startup.backend_runner) {
            startup.backend_runner->Stop();
        }
        startup.executor.reset();
        startup.backend_runner.reset();
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

    void ApplyTaskConfigDiffLocked(
        const AiConfig &next_config,
        std::vector<StoppedAiTask> &stopped_tasks) {
        if (!next_config.enabled) {
            StopTaskWorkersLocked(stopped_tasks);
            task_workers.clear();
            task_workers.reserve(next_config.tasks.size());
            for (const AiModelConfig &task_config : next_config.tasks) {
                std::shared_ptr<AiTaskWorker> task_worker(
                    new AiTaskWorker(task_config));
                task_worker->stats.enabled = false;
                task_worker->stats.alarm_linked = options.alarm != nullptr;
                task_workers.push_back(task_worker);
            }
            return;
        }

        std::vector<std::shared_ptr<AiTaskWorker>> next_workers;
        next_workers.reserve(next_config.tasks.size());
        for (const AiModelConfig &task_config : next_config.tasks) {
            std::shared_ptr<AiTaskWorker> task_worker =
                FindTaskWorkerLocked(task_config.task);
            if (task_worker && SameTaskConfig(task_worker->config,
                                             task_config)) {
                task_worker->stats.enabled = task_config.enabled;
                task_worker->stats.alarm_linked = options.alarm != nullptr;
                next_workers.push_back(task_worker);
                continue;
            }
            if (task_worker) {
                StopTaskWorkerLocked(task_worker, stopped_tasks);
            }
            std::shared_ptr<AiTaskWorker> next_task_worker(
                new AiTaskWorker(task_config));
            next_task_worker->stats.enabled = task_config.enabled;
            next_task_worker->stats.alarm_linked = options.alarm != nullptr;
            next_workers.push_back(next_task_worker);
        }

        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            if (!task_worker || HasTaskConfig(next_config.tasks,
                                              task_worker->config.task)) {
                continue;
            }
            StopTaskWorkerLocked(task_worker, stopped_tasks);
        }
        task_workers = next_workers;
    }

    std::shared_ptr<AiTaskWorker> FindTaskWorkerLocked(AiTask task) const {
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             task_workers) {
            if (task_worker && task_worker->config.task == task) {
                return task_worker;
            }
        }
        return std::shared_ptr<AiTaskWorker>();
    }

    AiStats TaskStatsLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker) const {
        if (!task_worker) {
            return AiStats{};
        }
        AiStats stats = task_worker->stats;
        stats.enabled = config.enabled && task_worker->config.enabled;
        stats.backend_available =
            stats.enabled && task_worker->running && task_worker->backend_runner &&
            task_worker->backend_runner->Available();
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
            summary.inferences += task_stats.inferences;
            summary.failed_inferences +=
                task_stats.failed_inferences;
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
        if (summary.inferences != 0) {
            summary.average_inference_time_ms = static_cast<uint32_t>(
                total_inference_time_ms / summary.inferences);
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
        if (task_worker->stats.inferences != 0) {
            task_worker->stats.average_inference_time_ms =
                static_cast<uint32_t>(
                    task_worker->inference_time_total_ms /
                    task_worker->stats.inferences);
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

    AiOptions options;
    AiConfig config;
    std::vector<std::shared_ptr<AiTaskWorker>> task_workers;
    AiFrameCapture frame_capture;
    AiAlertImages alert_images;
    bool config_attached = false;
    bool started = false;
    mutable std::mutex mutex;
};

AiTaskRunner::AiTaskRunner(const AiOptions &options) : state_(new State(options)) {}

AiTaskRunner::~AiTaskRunner() {
    state_->Release();
}

bool AiTaskRunner::Start() { return state_->Start(); }

void AiTaskRunner::Stop() {
    state_->Stop();
}

AiCapabilities AiTaskRunner::GetCapabilities() const {
    return BuildAiCapabilities();
}

AiConfig AiTaskRunner::GetConfig() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->config;
}

AiStats AiTaskRunner::GetStats() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->SummaryStatsLocked();
}

AiInferenceResult AiTaskRunner::GetLastResult() const {
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

std::vector<AiTaskInfo> AiTaskRunner::GetTaskInfoList() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<AiTaskInfo> statuses;
    statuses.reserve(state_->task_workers.size());
    for (const std::shared_ptr<AiTaskWorker> &task_worker :
         state_->task_workers) {
        if (!task_worker) {
            continue;
        }
        AiTaskInfo task_info;
        task_info.config = task_worker->config;
        task_info.stats = state_->TaskStatsLocked(task_worker);
        task_info.last_result = task_worker->last_result;
        statuses.push_back(task_info);
    }
    return statuses;
}

std::vector<AiAlertRecord> AiTaskRunner::ListAlerts() const {
    return state_->alert_images.List();
}

std::string AiTaskRunner::ReadAlertImage(const std::string &id) const {
    return state_->alert_images.ReadImage(id);
}

}  // namespace live_stream
