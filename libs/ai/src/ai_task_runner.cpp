#include "ai_task_runner.h"

#include "ai_alert_output.h"
#include "ai_backend_runner.h"
#include "ai_config.h"
#include "ai_config_binding.h"
#include "ai_defaults.h"
#include "ai_frame_capture.h"
#include "ai_task_startup.h"
#include "ai_task_workers.h"
#include "config.h"
#include "device.h"
#include "hisi_ai_platform.h"
#include "infra/log.h"
#include "infra/time.h"
#include "perimeter_filter.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr uint32_t kDefaultExecutorQueueCapacity = 8;
constexpr uint32_t kCaptureStopPollMs = 50;
constexpr int64_t kMinAlertIntervalMs = 1000;

using ai_internal::AiBackendRunner;
using ai_internal::AiAlertCapture;
using ai_internal::AiAlertOutput;
using ai_internal::AiConfigBinding;
using ai_internal::AiFrameCapture;
using ai_internal::AiTaskStartup;
using ai_internal::AiTaskWorker;
using ai_internal::AiTaskWorkers;
using ai_internal::BuildAiCapabilities;
using ai_internal::ClampAiInferenceTime;
using ai_internal::DefaultAiConfig;
using ai_internal::FilterPerimeterDetections;
using ai_internal::IsAiAlertResultActive;
using ai_internal::IsValidAiConfig;
using ai_internal::ParseAiConfig;
using ai_internal::StoppedAiTask;
using ai_internal::StopStoppedAiTask;
using ai_internal::StopStoppedAiTasks;

}  // namespace

struct AiTaskRunner::TaskRunnerInfo final {
    explicit TaskRunnerInfo(const AiOptions &service_options)
        : options(service_options),
          config(service_options.default_config),
          config_binding(service_options.config),
          frame_capture(service_options.snapshot, service_options.device),
          alert_output(service_options.alarm,
                       service_options.device,
                       service_options.alert_image_dir,
                       service_options.max_alert_records) {
        if (!IsValidAiConfig(config)) {
            config = DefaultAiConfig();
        }
        task_workers.Rebuild(config, alert_output.Linked());
    }

    bool Prepare() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!IsValidAiConfig(config)) {
                return false;
            }
            if (!config_binding.Attach(
                    [this]() {
                        std::lock_guard<std::mutex> guard(mutex);
                        return config;
                    },
                    [this](const AiConfig &next_config) {
                        return ApplyConfig(next_config);
                    })) {
                return false;
            }
        }

        AiConfig current_config;
        {
            std::lock_guard<std::mutex> lock(mutex);
            current_config = config;
        }
        AiConfig loaded_config;
        if (!config_binding.LoadInitial(current_config, &loaded_config)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            config = loaded_config;
            task_workers.Rebuild(config, alert_output.Linked());
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
            task_workers.Rebuild(config, alert_output.Linked());
        }

        StartConfiguredTaskWorkers();

        AiStats summary = SnapshotSummaryStats();
        std::size_t configured_task_size = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            configured_task_size = task_workers.Items().size();
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
            if (!started && task_workers.Items().empty()) {
                return;
            }
            started = false;
            task_workers.StopAll(&stopped_tasks);
        }
        StopStoppedAiTasks(&stopped_tasks);
        alert_output.StopImages();
        alert_output.ClearAlarmInput();
    }

    void Release() {
        Stop();
        config_binding.Detach();
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
            if (options.device == nullptr || !options.device->IsStarted() ||
                options.device->IsRestarting()) {
                infra::Time::SleepMillis(kCaptureStopPollMs);
                continue;
            }
            frame = frame_capture.Capture(run_config);

            if (!frame.Valid()) {
                MarkCaptureFailure(task_worker, run_config);
                PublishAlarmInputForTask(task_worker);
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
                IsAiAlertResultActive(result)
                    ? static_cast<uint32_t>(result.detections.size())
                    : 0;
        }

        PublishAlarmInputForTask(task_worker);
        return PrepareAlertCapture(result, run_config, task_worker,
                                   pending_alert);
    }

    bool PrepareAlertCapture(
        const AiInferenceResult &result,
        const AiModelConfig &run_config,
        const std::shared_ptr<AiTaskWorker> &task_worker,
        AiAlertCapture &pending_alert) {
        if (!IsAiAlertResultActive(result) ||
            !alert_output.CanCaptureImages()) {
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
        static_cast<void>(alert_output.PostCapture(pending_alert));
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
        bool service_started = false;
        bool service_enabled = false;
        std::vector<StoppedAiTask> stopped_tasks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            service_started = started;
            service_enabled = next_config.enabled;
            task_workers.ApplyConfigDiff(next_config,
                                         alert_output.Linked(),
                                         &stopped_tasks);
            config = next_config;
        }
        const bool task_workers_stopped = !stopped_tasks.empty();
        if (!service_enabled || task_workers_stopped) {
            alert_output.StopImages();
        }
        StopStoppedAiTasks(&stopped_tasks);

        if (!service_started) {
            return true;
        }

        StartConfiguredTaskWorkers();
        PublishAlarmInputForTask(nullptr);
        Info("ai", "AI config applied: enabled=%d tasks=%u enabled=%u",
             next_config.enabled ? 1 : 0,
             static_cast<unsigned int>(next_config.tasks.size()),
             static_cast<unsigned int>(enabled_task_size));
        if (!next_config.enabled) {
            alert_output.ClearAlarmInput();
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
                task_workers.MarkAllEnabledBackendsUnavailable(
                    alert_output.Linked());
                return;
            }
            if (!alert_output.StartImages()) {
                Error("ai", "AI startup skipped: alert executor unavailable");
                task_workers.MarkAllEnabledBackendsUnavailable(
                    alert_output.Linked());
                return;
            }
            enabled_task_workers = task_workers.EnabledToStart(
                config, alert_output.Linked());
        }

        Info("ai", "AI enabled task startup size=%u",
             static_cast<unsigned int>(enabled_task_workers.size()));
        for (const std::shared_ptr<AiTaskWorker> &task_worker :
             enabled_task_workers) {
            AiTaskStartup startup;
            if (!startup.Prepare(task_worker, kDefaultExecutorQueueCapacity)) {
                MarkTaskBackendUnavailable(task_worker);
                continue;
            }

            StoppedAiTask failed_task;
            if (!CommitAiTaskStartup(startup, failed_task)) {
                startup.Stop();
                StopStoppedAiTask(&failed_task);
            }
        }
    }

    bool CommitAiTaskStartup(AiTaskStartup &startup,
                             StoppedAiTask &failed_task) {
        if (!startup.TaskWorker() || !startup.BackendRunner()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex);
        std::shared_ptr<AiTaskWorker> task_worker = startup.TaskWorker();
        if (!CanStartTaskWorkerLocked(task_worker)) {
            return false;
        }

        task_worker->backend_runner = startup.BackendRunner();
        task_worker->executor = startup.TakeExecutor();
        if (!task_worker->executor) {
            task_worker->backend_runner.reset();
            return false;
        }
        task_worker->stats.enabled = true;
        task_worker->stats.backend_available =
            startup.BackendRunner()->Available();
        task_worker->running = true;
        startup.ReleaseBackendRunner();
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

    bool CanStartTaskWorkerLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker) const {
        if (!task_worker || !started || !config.enabled ||
            !task_worker->config.enabled) {
            return false;
        }
        for (const std::shared_ptr<AiTaskWorker> &current :
             task_workers.Items()) {
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

    AiStats TaskStatsLocked(
        const std::shared_ptr<AiTaskWorker> &task_worker) const {
        return task_workers.TaskStats(config, task_worker,
                                      alert_output.Linked());
    }

    AiStats SummaryStatsLocked() const {
        return task_workers.SummaryStats(config, alert_output.Linked());
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
            ClampAiInferenceTime(inference_time_ms);
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
        return task_workers.BuildAlarmInput();
    }

    void PublishAlarmInputForTask(
        const std::shared_ptr<AiTaskWorker> &task_worker) {
        AlarmInput input;
        {
            std::lock_guard<std::mutex> lock(mutex);
            input = AlarmInputLocked();
        }
        if (!alert_output.PublishAlarmInput(input)) {
            IncrementDroppedTasks(task_worker);
        }
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
    AiConfigBinding config_binding;
    AiTaskWorkers task_workers;
    AiFrameCapture frame_capture;
    AiAlertOutput alert_output;
    bool started = false;
    mutable std::mutex mutex;
};

AiTaskRunner::AiTaskRunner(const AiOptions &options)
    : info_(new TaskRunnerInfo(options)) {}

AiTaskRunner::~AiTaskRunner() {
    info_->Release();
}

bool AiTaskRunner::Start() { return info_->Start(); }

void AiTaskRunner::Stop() {
    info_->Stop();
}

AiCapabilities AiTaskRunner::GetCapabilities() const {
    return BuildAiCapabilities();
}

AiConfig AiTaskRunner::GetConfig() const {
    std::lock_guard<std::mutex> lock(info_->mutex);
    return info_->config;
}

AiStats AiTaskRunner::GetStats() const {
    std::lock_guard<std::mutex> lock(info_->mutex);
    return info_->SummaryStatsLocked();
}

AiInferenceResult AiTaskRunner::GetLastResult() const {
    std::lock_guard<std::mutex> lock(info_->mutex);
    return info_->task_workers.LatestResult();
}

std::vector<AiTaskInfo> AiTaskRunner::GetTaskInfoList() const {
    std::lock_guard<std::mutex> lock(info_->mutex);
    std::vector<AiTaskInfo> statuses;
    statuses.reserve(info_->task_workers.Items().size());
    for (const std::shared_ptr<AiTaskWorker> &task_worker :
         info_->task_workers.Items()) {
        if (!task_worker) {
            continue;
        }
        AiTaskInfo task_info;
        task_info.config = task_worker->config;
        task_info.stats = info_->TaskStatsLocked(task_worker);
        task_info.last_result = task_worker->last_result;
        statuses.push_back(task_info);
    }
    return statuses;
}

std::vector<AiAlertRecord> AiTaskRunner::ListAlerts() const {
    return info_->alert_output.ListImages();
}

std::string AiTaskRunner::ReadAlertImage(const std::string &id) const {
    return info_->alert_output.ReadImage(id);
}

}  // namespace live_stream
