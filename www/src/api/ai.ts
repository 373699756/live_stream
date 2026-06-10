import {
  putJson,
  requestJson,
  type ApiRequestOptions,
} from './client';
import { mockAiAlerts, mockAiStatus } from './mockAi';
import type { AiAlertList, AiConfig, AiStatus } from './types';

export function getAiStatus(init?: ApiRequestOptions): Promise<AiStatus> {
  return requestJson<AiStatus>('/api/ai/status', mockAiStatus, init).then(
    normalizeAiStatus,
  );
}

export function getAiAlerts(init?: ApiRequestOptions): Promise<AiAlertList> {
  return requestJson<AiAlertList>('/api/ai/alerts', mockAiAlerts, init);
}

export function saveAiConfig(value: AiConfig): Promise<void> {
  return putJson('/api/config/ai', value);
}

export function aiAlertImageUrl(imageUrl: string, timestampMs?: number): string {
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
  };
}
