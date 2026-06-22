import { putJson, requestJson, type ApiRequestOptions } from './client';
import { mockAiAlerts, mockAiCapabilities, mockAiStatus } from './mockAi';
import type {
    AiAlertList,
    AiCapabilities,
    AiConfig,
    AiStatus,
    AiTaskCapability,
    AiTaskName,
} from './types';

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
        capabilities: normalizeAiCapabilities(
            status.capabilities ?? mockAiCapabilities,
        ),
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
    const fallback = mockAiCapabilities.tasks.find(
        (task) => task.task === capability.task,
    );
    const requiresModel =
        capability.requires_model ??
        (capability.task === 'object_detection' ||
            capability.task === 'perimeter_detection');
    return {
        ...(fallback ?? mockAiCapabilities.tasks[0]),
        ...capability,
        available: capability.available ?? true,
        requires_model: requiresModel,
        unavailable_reason: capability.unavailable_reason ?? '',
        supported_backends:
            capability.supported_backends?.length
                ? capability.supported_backends
                : ['hisi3516dv300_nnie'],
        supported_streams:
            capability.supported_streams?.length
                ? capability.supported_streams
                : ['sub', 'main'],
    };
}

function normalizeAiCapabilities(
    capabilities: Partial<AiCapabilities>,
): AiCapabilities {
    const taskItems = capabilities.tasks ?? [];
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
        available:
            capabilities.available ?? tasks.some((task) => task.available),
        model_runtime_available:
            capabilities.model_runtime_available ??
            tasks.some((task) => task.available),
        model_runtime_reason: capabilities.model_runtime_reason ?? '',
        tasks,
    };
}
