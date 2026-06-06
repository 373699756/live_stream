// System status API

import { mockSystemStatus } from './mockSystem';
import { requestJson } from './client';
import type { SystemStatus } from './types';

export function getSystemStatus(): Promise<SystemStatus> {
  return requestJson<SystemStatus>('/api/system/status', mockSystemStatus);
}
