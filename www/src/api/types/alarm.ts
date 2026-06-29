export interface AlarmRuleConfig {
    enabled: boolean;
    sensitivity: number;
    min_duration_ms: number;
    repeat_interval_ms?: number;
    manual_clear?: boolean;
    level?: number;
    regions: unknown[];
    [key: string]: unknown;
}

export interface AlarmActionsConfig {
    snapshot: boolean;
    notify: boolean;
    [key: string]: unknown;
}

export interface AlarmScheduleConfig {
    mode: string;
    weekly: unknown[];
    [key: string]: unknown;
}

export interface AlarmConfig {
    motion_detection: AlarmRuleConfig;
    ai_detection: AlarmRuleConfig;
    actions: AlarmActionsConfig;
    schedule: AlarmScheduleConfig;
    [key: string]: unknown;
}

export type AlarmSourceName =
    | 'motion'
    | 'ai_detection'
    | 'io_input'
    | 'tamper'
    | 'network'
    | 'unknown';

export interface AlarmSourceState {
    source: AlarmSourceName;
    enabled: boolean;
    waiting: boolean;
    active: boolean;
    waiting_since_ms: number;
    active_since_ms: number;
    last_alarm_time_ms: number;
    level: number;
    message: string;
}

export interface AlarmInfo {
    active: boolean;
    source: AlarmSourceName;
    active_since_ms: number;
    last_trigger_time_ms: number;
    level: number;
    message: string;
    sources: AlarmSourceState[];
}

export interface AlarmInfoResponse {
    available: boolean;
    status: AlarmInfo;
}
