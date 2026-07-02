import { putJson, requestJson, type ApiRequestOptions } from './client';
import { mockAiAlerts, mockAiCapabilities, mockAiStatus } from './mockAi';
import type {
    AiAlertList,
    AiCapabilities,
    AiConfig,
    AiStatus,
    AiTaskCapability,
    AiTaskName,
} from './types/ai';

export function getAiStatus(init?: ApiRequestOptions): Promise<AiStatus> {
    return requestJson<AiStatus>('/api/ai/status', mockAiStatus, init).then(
        normalizeAiStatus,
    );
}

export function getAiAlerts(init?: ApiRequestOptions): Promise<AiAlertList> {
    return requestJson<AiAlertList>('/api/ai/alerts', mockAiAlerts, init);
}

export function getAiCapabilities(
    init?: ApiRequestOptions,
): Promise<AiCapabilities> {
    return requestJson<AiCapabilities>(
        '/api/ai/capabilities',
        mockAiCapabilities,
        init,
    ).then(normalizeAiCapabilities);
}

export function saveAiConfig(value: AiConfig): Promise<void> {
    return putJson('/api/config/ai', value);
}

export function aiAlertImageUrl(
    imageUrl: string,
    timestampMs?: number,
): string {
    if (!timestampMs) {
        return imageUrl;
    }
    const separator = imageUrl.includes('?') ? '&' : '?';
    return `${imageUrl}${separator}t=${timestampMs}`;
}

function normalizeAiStatus(status: AiStatus): AiStatus {
    const normalizedTasks = (status.tasks ?? []).map((task) => ({
        ...task,
        config: {
            ...task.config,
            perimeter_regions: task.config.perimeter_regions ?? [],
        },
        last_result: {
            ...task.last_result,
            detections: task.last_result.detections ?? [],
        },
    }));
    return {
        ...status,
        enabled: status.enabled ?? status.config.enabled,
        config: {
            ...status.config,
            tasks: (status.config.tasks ?? []).map((task) => ({
                ...task,
                perimeter_regions: task.perimeter_regions ?? [],
            })),
        },
        summary: status.summary,
        tasks: normalizedTasks,
        last_result: {
            ...status.last_result,
            detections: status.last_result.detections ?? [],
        },
        capabilities: normalizeAiCapabilities(status.capabilities),
    };
}

const kAiTaskNames: AiTaskName[] = [
    'object_detection',
    'perimeter_detection',
    'motion_classification',
    'occlusion_detection',
];

function normalizeAiTaskCapability(
    capability: Partial<AiTaskCapability> & { task: AiTaskName },
): AiTaskCapability {
    const requiresModel =
        capability.requires_model ??
        (capability.task === 'object_detection' ||
            capability.task === 'perimeter_detection');
    return {
        available: capability.available === true,
        default_model_path: capability.default_model_path ?? '',
        default_input_height: capability.default_input_height ?? 0,
        default_input_width: capability.default_input_width ?? 0,
        default_inference_interval_ms:
            capability.default_inference_interval_ms ?? 0,
        default_max_results: capability.default_max_results ?? 0,
        default_confidence_threshold:
            capability.default_confidence_threshold ?? 0,
        max_confidence_threshold: capability.max_confidence_threshold ?? 0,
        max_inference_interval_ms: capability.max_inference_interval_ms ?? 0,
        max_perimeter_regions: capability.max_perimeter_regions ?? 0,
        max_results: capability.max_results ?? 0,
        min_confidence_threshold: capability.min_confidence_threshold ?? 0,
        min_inference_interval_ms: capability.min_inference_interval_ms ?? 0,
        min_results: capability.min_results ?? 0,
        task: capability.task,
        requires_model: requiresModel,
        unavailable_reason: capability.unavailable_reason ?? '',
        supported_backends:
            capability.supported_backends?.length
                ? capability.supported_backends
                : [],
        supported_streams:
            capability.supported_streams?.length
                ? capability.supported_streams
                : [],
    };
}

function normalizeAiCapabilities(
    capabilities: Partial<AiCapabilities> | undefined,
): AiCapabilities {
    const taskItems = capabilities?.tasks ?? [];
    const tasks = kAiTaskNames.map((taskName) => {
        const taskCapability = taskItems.find(
            (item) => item.task === taskName,
        );
        return normalizeAiTaskCapability({
            ...(taskCapability ?? { task: taskName }),
            task: taskName,
        });
    });
    return {
        available: capabilities?.available === true,
        model_runtime_available:
            capabilities?.model_runtime_available === true,
        model_runtime_reason:
            capabilities?.model_runtime_reason ??
            (capabilities ? '' : 'ai_capabilities_missing'),
        tasks,
    };
}
