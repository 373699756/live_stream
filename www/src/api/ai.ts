import {
  putJson,
  requestJson,
  type ApiRequestOptions,
} from './client';
import { mockAiAlerts, mockAiStatus } from './mockAi';
import type { AiAlertList, AiModelConfig, AiStatus } from './types';

export function getAiStatus(init?: ApiRequestOptions): Promise<AiStatus> {
  return requestJson<AiStatus>('/api/ai/status', mockAiStatus, init).then(
    normalizeAiStatus,
  );
}

export function getAiAlerts(init?: ApiRequestOptions): Promise<AiAlertList> {
  return requestJson<AiAlertList>('/api/ai/alerts', mockAiAlerts, init);
}

export function saveAiConfig(value: AiModelConfig): Promise<void> {
  return putJson('/api/config/ai', value);
}

export function aiAlertImageUrl(imageUrl: string): string {
  return imageUrl;
}

function normalizeAiStatus(status: AiStatus): AiStatus {
  return {
    ...status,
    config: {
      ...status.config,
      perimeter_regions: status.config.perimeter_regions ?? [],
    },
  };
}
