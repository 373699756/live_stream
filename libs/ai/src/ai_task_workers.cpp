#include "ai_task_workers.h"

#include <algorithm>
#include <limits>
#include <string>

namespace live_stream {
namespace ai_internal {
namespace {

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

}  // namespace

AiTaskWorker::AiTaskWorker(const AiModelConfig &task_config)
    : config(task_config) {}

bool IsAiAlertResultActive(const AiInferenceResult &result) {
    return result.success && !result.detections.empty();
}

const char *AiTaskAlarmName(AiTask task) {
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

uint32_t ClampAiInferenceTime(int64_t inference_time_ms) {
    if (inference_time_ms <= 0) {
        return 0;
    }
    return static_cast<uint32_t>(std::min<int64_t>(
        inference_time_ms,
        static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
}

bool SameAiTaskConfig(const AiModelConfig &lhs, const AiModelConfig &rhs) {
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

void AiTaskWorkers::Rebuild(const AiConfig &config, bool alarm_linked) {
    workers_.clear();
    workers_.reserve(config.tasks.size());
    for (const AiModelConfig &task_config : config.tasks) {
        std::shared_ptr<AiTaskWorker> task_worker(
            new AiTaskWorker(task_config));
        task_worker->stats.enabled = config.enabled && task_config.enabled;
        task_worker->stats.alarm_linked = alarm_linked;
        workers_.push_back(task_worker);
    }
}

void AiTaskWorkers::ApplyConfigDiff(
    const AiConfig &next_config,
    bool alarm_linked,
    std::vector<StoppedAiTask> *stopped_tasks) {
    if (stopped_tasks == nullptr) {
        return;
    }
    if (!next_config.enabled) {
        StopAll(stopped_tasks);
        workers_.clear();
        workers_.reserve(next_config.tasks.size());
        for (const AiModelConfig &task_config : next_config.tasks) {
            std::shared_ptr<AiTaskWorker> task_worker(
                new AiTaskWorker(task_config));
            task_worker->stats.enabled = false;
            task_worker->stats.alarm_linked = alarm_linked;
            workers_.push_back(task_worker);
        }
        return;
    }

    std::vector<std::shared_ptr<AiTaskWorker>> next_workers;
    next_workers.reserve(next_config.tasks.size());
    for (const AiModelConfig &task_config : next_config.tasks) {
        std::shared_ptr<AiTaskWorker> task_worker = Find(task_config.task);
        if (task_worker &&
            SameAiTaskConfig(task_worker->config, task_config)) {
            task_worker->stats.enabled = task_config.enabled;
            task_worker->stats.alarm_linked = alarm_linked;
            next_workers.push_back(task_worker);
            continue;
        }
        if (task_worker) {
            StopWorker(task_worker, stopped_tasks);
        }
        std::shared_ptr<AiTaskWorker> next_task_worker(
            new AiTaskWorker(task_config));
        next_task_worker->stats.enabled = task_config.enabled;
        next_task_worker->stats.alarm_linked = alarm_linked;
        next_workers.push_back(next_task_worker);
    }

    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        if (!task_worker ||
            HasTaskConfig(next_config.tasks, task_worker->config.task)) {
            continue;
        }
        StopWorker(task_worker, stopped_tasks);
    }
    workers_ = next_workers;
}

void AiTaskWorkers::StopAll(std::vector<StoppedAiTask> *stopped_tasks) {
    if (stopped_tasks == nullptr) {
        return;
    }
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        StopWorker(task_worker, stopped_tasks);
    }
}

void AiTaskWorkers::StopWorker(
    const std::shared_ptr<AiTaskWorker> &task_worker,
    std::vector<StoppedAiTask> *stopped_tasks) {
    if (!task_worker || stopped_tasks == nullptr) {
        return;
    }
    task_worker->running = false;
    task_worker->stats.backend_available = false;
    StoppedAiTask stopped;
    stopped.executor = std::move(task_worker->executor);
    stopped.backend_runner = std::move(task_worker->backend_runner);
    if (stopped.executor || stopped.backend_runner) {
        stopped_tasks->push_back(std::move(stopped));
    }
}

std::shared_ptr<AiTaskWorker> AiTaskWorkers::Find(AiTask task) const {
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        if (task_worker && task_worker->config.task == task) {
            return task_worker;
        }
    }
    return std::shared_ptr<AiTaskWorker>();
}

const std::vector<std::shared_ptr<AiTaskWorker>> &AiTaskWorkers::Items()
    const {
    return workers_;
}

std::vector<std::shared_ptr<AiTaskWorker>> AiTaskWorkers::EnabledToStart(
    const AiConfig &config,
    bool alarm_linked) {
    std::vector<std::shared_ptr<AiTaskWorker>> enabled_workers;
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        if (!task_worker->config.enabled) {
            task_worker->stats.enabled = false;
            task_worker->stats.alarm_linked = alarm_linked;
            continue;
        }
        task_worker->stats.enabled = config.enabled;
        task_worker->stats.alarm_linked = alarm_linked;
        if (task_worker->running) {
            continue;
        }
        task_worker->stats.backend_available = false;
        enabled_workers.push_back(task_worker);
    }
    return enabled_workers;
}

void AiTaskWorkers::MarkAllEnabledBackendsUnavailable(bool alarm_linked) {
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        if (!task_worker || !task_worker->config.enabled) {
            continue;
        }
        task_worker->stats.enabled = true;
        task_worker->stats.alarm_linked = alarm_linked;
        task_worker->stats.backend_available = false;
    }
}

AiStats AiTaskWorkers::TaskStats(
    const AiConfig &config,
    const std::shared_ptr<AiTaskWorker> &task_worker,
    bool alarm_linked) const {
    if (!task_worker) {
        return AiStats{};
    }
    AiStats stats = task_worker->stats;
    stats.enabled = config.enabled && task_worker->config.enabled;
    stats.backend_available =
        stats.enabled && task_worker->running && task_worker->backend_runner &&
        task_worker->backend_runner->Available();
    stats.alarm_linked = alarm_linked;
    stats.active_results =
        stats.enabled && IsAiAlertResultActive(task_worker->last_result)
            ? static_cast<uint32_t>(task_worker->last_result.detections.size())
            : 0;
    return stats;
}

AiStats AiTaskWorkers::SummaryStats(const AiConfig &config,
                                    bool alarm_linked) const {
    AiStats summary;
    summary.enabled = config.enabled;
    summary.alarm_linked = alarm_linked;
    bool any_enabled_task = false;
    bool all_enabled_backends_available = true;
    uint64_t total_inference_time_ms = 0;
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        const AiStats task_stats = TaskStats(config, task_worker, alarm_linked);
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
        summary.failed_inferences += task_stats.failed_inferences;
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

AiInferenceResult AiTaskWorkers::LatestResult() const {
    AiInferenceResult latest_result;
    int64_t latest_result_time_ms = 0;
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
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

AlarmInput AiTaskWorkers::BuildAlarmInput() const {
    AlarmInput input;
    input.source = AlarmSource::kAiDetection;
    int64_t latest_active_result_time_ms = 0;
    AiTask latest_active_task = AiTask::kObjectDetection;
    for (const std::shared_ptr<AiTaskWorker> &task_worker : workers_) {
        if (!task_worker ||
            !IsAiAlertResultActive(task_worker->last_result)) {
            continue;
        }
        input.active = true;
        input.value += static_cast<int32_t>(
            task_worker->last_result.detections.size());
        if (task_worker->last_result_time_ms >=
            latest_active_result_time_ms) {
            latest_active_result_time_ms = task_worker->last_result_time_ms;
            latest_active_task = task_worker->config.task;
        }
    }
    if (input.active) {
        input.message =
            std::string("ai_") + AiTaskAlarmName(latest_active_task);
    }
    return input;
}

}  // namespace ai_internal
}  // namespace live_stream
