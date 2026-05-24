import { authQuery, requestJson } from './client';
import { mockAiAlerts, mockAiStatus } from './mock';
import type { AiAlertList, AiStatus } from './types';

export function getAiStatus(): Promise<AiStatus> {
  return requestJson<AiStatus>('/api/ai/status', mockAiStatus);
}

export function getAiAlerts(): Promise<AiAlertList> {
  return requestJson<AiAlertList>('/api/ai/alerts', mockAiAlerts);
}

export function aiAlertImageUrl(imageUrl: string): string {
  const query = authQuery();
  const separator = imageUrl.includes('?') ? '&' : '?';
  return `${imageUrl}${query ? `${separator}${query}` : ''}`;
}
