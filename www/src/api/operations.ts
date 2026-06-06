import { authQuery, requestJson } from './client';
import type { OperationRecord } from './types';

export function getOperations(): Promise<{ items: OperationRecord[] }> {
  return requestJson<{ items: OperationRecord[] }>('/api/operations', { items: [] });
}

export function operationsExportUrl(): string {
  const query = authQuery();
  return `/api/operations/export${query ? `?${query}` : ''}`;
}
