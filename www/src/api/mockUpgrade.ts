import type { UpgradeStatus } from './types';

export const mockUpgradeStatus: UpgradeStatus = {
  state: 'idle',
  progress_percent: 0,
  current_stage: 'idle',
  target_version: '',
  ok: true,
  error_message: '',
  started_at_ms: 0,
  finished_at_ms: 0,
};
