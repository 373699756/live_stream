import {
  authQuery,
  putJson,
  requestJson,
  type ApiRequestOptions,
} from './client';
import { mockAiAlerts, mockAiStatus } from './mockAi';
import type { AiAlertList, AiModelConfig, AiStatus } from './types';

export function getAiStatus(init?: ApiRequestOptions): Promise<AiStatus> {
  return requestJson<AiStatus>('/api/ai/status', mockAiStatus, init);
}

export function getAiAlerts(): Promise<AiAlertList> {
  return requestJson<AiAlertList>('/api/ai/alerts', mockAiAlerts);
}

export function saveAiConfig(value: AiModelConfig): Promise<void> {
  return putJson('/api/config/ai', value);
}

export function aiAlertImageUrl(imageUrl: string): string {
  const query = authQuery();
  const separator = imageUrl.includes('?') ? '&' : '?';
  return `${imageUrl}${query ? `${separator}${query}` : ''}`;
}
