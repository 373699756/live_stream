import type {
    AiAlertRecord,
    AiCapabilities,
    AiConfig,
    AiModelConfig,
    AiStats,
    AiTaskInfo,
    AiTaskName,
    StreamName,
} from '../../api/types';
import {
    capabilityForTask,
    isAiTaskAvailable,
    taskUnavailableText,
} from './aiAlertTasks';
import {
    kSensitivityOptions,
    kTaskOrder,
    type SensitivityLevel,
} from './aiAlertOptions';

export function defaultTaskConfig(
    task: AiTaskName,
    capabilities?: AiCapabilities | null,
): AiModelConfig {
    const capability = capabilityForTask(capabilities, task);
    const requiresModel =
        capability?.requires_model ??
        (task === 'object_detection' || task === 'perimeter_detection');
    return {
        enabled: false,
        backend: capability?.supported_backends[0] ?? 'hisi3516dv300_nnie',
        task,
        stream: capability?.supported_streams[0] ?? 'sub',
        model_path: requiresModel ? (capability?.default_model_path ?? '') : '',
        input_width: capability?.default_input_width ?? 300,
        input_height: capability?.default_input_height ?? 300,
        inference_interval_ms:
            capability?.default_inference_interval_ms ?? 500,
        confidence_threshold:
            capability?.default_confidence_threshold ?? 0.5,
        max_results: capability?.default_max_results ?? 16,
        perimeter_regions: [],
    };
}

export function cloneTaskConfig(task: AiModelConfig): AiModelConfig {
    return {
        ...task,
        perimeter_regions: (task.perimeter_regions ?? []).map((region) => ({
            ...region,
        })),
    };
}

export function positiveConfigInteger(value: number, fallback: number) {
    return Number.isFinite(value) && value > 0 ? Math.round(value) : fallback;
}

export function optionValue(value: number) {
    return String(Math.round(value));
}

export function numericOptionsWithCurrent(
    options: Array<{ label: string; value: number }>,
    currentValue: number,
    label: (value: number) => string,
) {
    const roundedValue = Math.round(currentValue);
    if (options.some((option) => option.value === roundedValue)) {
        return options;
    }
    return [...options, { label: label(roundedValue), value: roundedValue }];
}

export function draftTaskByName(config: AiConfig | null, name: AiTaskName) {
    return config?.tasks.find((task) => task.task === name);
}

export function sharedHiddenTaskConfig(
    config: AiConfig,
    capabilities?: AiCapabilities | null,
) {
    const fallback = defaultTaskConfig('object_detection', capabilities);
    const source =
        config.tasks.find((task) =>
            isAiTaskAvailable(task.task, capabilities),
        ) ??
        draftTaskByName(config, 'object_detection') ??
        fallback;
    return {
        backend: source.backend,
        stream: source.stream,
        model_path: source.model_path.trim() || fallback.model_path,
        input_width: positiveConfigInteger(
            source.input_width,
            fallback.input_width,
        ),
        input_height: positiveConfigInteger(
            source.input_height,
            fallback.input_height,
        ),
        inference_interval_ms: positiveConfigInteger(
            source.inference_interval_ms,
            fallback.inference_interval_ms,
        ),
        max_results: positiveConfigInteger(
            source.max_results,
            fallback.max_results,
        ),
    };
}

export function withSharedHiddenDefaults(
    config: AiConfig,
    capabilities?: AiCapabilities | null,
): AiConfig {
    const shared = sharedHiddenTaskConfig(config, capabilities);
    const threshold = thresholdForSensitivity(
        sensitivityForConfig(config, capabilities),
    );
    return {
        ...config,
        tasks: config.tasks.map((task) => {
            const fallback = defaultTaskConfig(task.task, capabilities);
            return {
                ...fallback,
                ...task,
                ...shared,
                confidence_threshold: threshold,
                perimeter_regions:
                    task.task === 'perimeter_detection'
                        ? (task.perimeter_regions ?? [])
                        : [],
            };
        }),
    };
}

export function completeAiConfig(
    config: AiConfig,
    capabilities?: AiCapabilities | null,
): AiConfig {
    const tasks = kTaskOrder.map((taskName) =>
        cloneTaskConfig(
            config.tasks.find((task) => task.task === taskName) ??
                defaultTaskConfig(taskName, capabilities),
        ),
    );
    return withSharedHiddenDefaults(
        {
            ...config,
            enabled: tasks.some(
                (task) =>
                    task.enabled &&
                    isAiTaskAvailable(task.task, capabilities),
            ),
            tasks,
        },
        capabilities,
    );
}

export function emptyStats(): AiStats {
    return {
        enabled: false,
        backend_available: false,
        alarm_linked: false,
        last_success_time_ms: 0,
        last_failure_time_ms: 0,
        received_frames: 0,
        skipped_frames: 0,
        inferences: 0,
        failed_inferences: 0,
        dropped_tasks: 0,
        last_inference_time_ms: 0,
        max_inference_time_ms: 0,
        average_inference_time_ms: 0,
        active_results: 0,
    };
}

export function streamLabel(stream: StreamName) {
    return stream === 'main' ? '主码流' : '子码流';
}

export function numberText(value: number) {
    return Number.isFinite(value) ? String(Math.round(value)) : '--';
}

export function clampUnit(value: number) {
    if (!Number.isFinite(value)) {
        return 0;
    }
    return Math.min(1, Math.max(0, value));
}

export function formatPercent(value: number) {
    return `${Math.round(clampUnit(value) * 100)}%`;
}

export function maxConfidence(alert: AiAlertRecord) {
    return `${Math.round(alert.confidence_max * 100)}%`;
}

export function taskByName(tasks: AiTaskInfo[], name: AiTaskName) {
    return tasks.find((task) => task.config.task === name);
}

export function updateTaskConfig(
    config: AiConfig,
    taskName: AiTaskName,
    patch: Partial<AiModelConfig>,
): AiConfig {
    return {
        ...config,
        tasks: config.tasks.map((task) =>
            task.task === taskName ? { ...task, ...patch } : task,
        ),
    };
}

export function taskStatusText(
    task: AiTaskInfo | undefined,
    capabilities?: AiCapabilities | null,
) {
    if (!task) {
        return '未配置';
    }
    if (!isAiTaskAvailable(task.config.task, capabilities)) {
        return taskUnavailableText(task.config.task, capabilities);
    }
    if (!task.config.enabled) {
        return '关闭';
    }
    if (!task.stats.enabled) {
        return '未运行';
    }
    if (!task.stats.backend_available) {
        return '后端异常';
    }
    return '运行';
}

export function taskBadgeState(
    task: AiTaskInfo | undefined,
    capabilities?: AiCapabilities | null,
) {
    if (task && !isAiTaskAvailable(task.config.task, capabilities)) {
        return 'pending' as const;
    }
    if (!task || !task.config.enabled || !task.stats.enabled) {
        return 'pending' as const;
    }
    return task.stats.backend_available
        ? ('running' as const)
        : ('error' as const);
}

export function sensitivityFromThreshold(
    threshold: number,
): SensitivityLevel {
    let closest = kSensitivityOptions[1];
    for (const option of kSensitivityOptions) {
        if (
            Math.abs(option.threshold - threshold) <
            Math.abs(closest.threshold - threshold)
        ) {
            closest = option;
        }
    }
    return closest.value;
}

export function thresholdForSensitivity(value: SensitivityLevel) {
    return (
        kSensitivityOptions.find((option) => option.value === value) ??
        kSensitivityOptions[1]
    ).threshold;
}

export function sensitivityForConfig(
    config: AiConfig | null,
    capabilities?: AiCapabilities | null,
): SensitivityLevel {
    const sourceTask =
        config?.tasks.find((task) =>
            isAiTaskAvailable(task.task, capabilities),
        ) ?? config?.tasks[0];
    return sensitivityFromThreshold(sourceTask?.confidence_threshold ?? 0.5);
}

export function alertSizesByTask(alerts: AiAlertRecord[]) {
    const sizes: Record<AiTaskName, number> = {
        object_detection: 0,
        perimeter_detection: 0,
        motion_classification: 0,
        occlusion_detection: 0,
    };
    alerts.forEach((alert) => {
        sizes[alert.task] += 1;
    });
    return sizes;
}
