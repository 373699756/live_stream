export type TimeSyncSource = 'manual' | 'onvif' | 'ntp' | 'browser' | 'unknown';

export interface NtpConfig {
    enabled: boolean;
    servers: string[];
    sync_interval_sec: number;
}

export interface TimeInfo {
    system_time_ms: number;
    timezone: string;
    ntp: NtpConfig;
    manual_sync_allowed: boolean;
    browser_sync_on_login: boolean;
    last_sync_source: TimeSyncSource;
    last_sync_time_ms: number;
    last_sync_ok: boolean;
}

export interface TimeConfig {
    timezone: string;
    ntp: NtpConfig;
    manual_sync_allowed: boolean;
    browser_sync_on_login: boolean;
}

export interface BrowserSyncConfig {
    manual_sync_allowed: boolean;
    browser_sync_on_login: boolean;
}
