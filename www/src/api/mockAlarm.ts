import type { AlarmConfig, AlarmStatusResponse } from './types';

export const mockAlarmConfig: AlarmConfig = {
  motion_detection: {
    enabled: false,
    sensitivity: 50,
    min_duration_ms: 500,
    regions: [],
  },
  ai_detection: {
    enabled: false,
    sensitivity: 50,
    min_duration_ms: 0,
    regions: [],
  },
  actions: {
    snapshot: true,
    notify: true,
  },
  schedule: {
    mode: 'always',
    weekly: [],
  },
};

export const mockAlarmStatus: AlarmStatusResponse = {
  available: true,
  status: {
    active: false,
    source: 'ai_detection',
    active_since_ms: 0,
    last_trigger_time_ms: 0,
    message: '',
  },
};
