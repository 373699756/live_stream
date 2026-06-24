import type { AlarmConfig, AlarmInfoResponse } from './types';

export const mockAlarmConfig: AlarmConfig = {
    motion_detection: {
        enabled: false,
        sensitivity: 50,
        min_duration_ms: 500,
        repeat_interval_ms: 0,
        manual_clear: false,
        level: 1,
        regions: [],
    },
    ai_detection: {
        enabled: false,
        sensitivity: 50,
        min_duration_ms: 0,
        repeat_interval_ms: 0,
        manual_clear: false,
        level: 1,
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

export const mockAlarmInfo: AlarmInfoResponse = {
    available: true,
    status: {
        active: false,
        source: 'ai_detection',
        active_since_ms: 0,
        last_trigger_time_ms: 0,
        level: 0,
        message: '',
        sources: [
            {
                source: 'motion',
                enabled: false,
                waiting: false,
                active: false,
                waiting_since_ms: 0,
                active_since_ms: 0,
                last_alarm_time_ms: 0,
                level: 1,
                message: '',
            },
            {
                source: 'ai_detection',
                enabled: false,
                waiting: false,
                active: false,
                waiting_since_ms: 0,
                active_since_ms: 0,
                last_alarm_time_ms: 0,
                level: 1,
                message: '',
            },
        ],
    },
};
