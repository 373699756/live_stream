import type { SnapshotConfig } from './types';

export const mockSnapshotConfig: SnapshotConfig = {
  enabled: true,
  main_path: '/api/snapshot/main.jpg',
  sub_path: '/api/snapshot/sub.jpg',
  jpeg_quality: 85,
  timeout_ms: 2000,
};
