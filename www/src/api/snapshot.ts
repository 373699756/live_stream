import { mockSnapshotConfig } from './mock';
import { requestJson, putJson } from './client';
import type { SnapshotConfig } from './types';

export function getSnapshotConfig(): Promise<SnapshotConfig> {
  return requestJson<SnapshotConfig>('/api/config/snapshot', mockSnapshotConfig);
}

export function saveSnapshotConfig(value: SnapshotConfig): Promise<void> {
  return putJson('/api/config/snapshot', value);
}
