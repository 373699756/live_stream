import type { TimeStatus } from './types';

export const mockTimeStatus: TimeStatus = {
    system_time_ms: Date.now() - 32000,
    timezone: 'Asia/Shanghai',
    ntp: {
        enabled: true,
        servers: ['pool.ntp.org', 'ntp.aliyun.com'],
        sync_interval_sec: 3600,
    },
    manual_sync_allowed: true,
    browser_sync_on_login: true,
    last_sync_source: 'browser',
    last_sync_time_ms: Date.now() - 32000,
    last_sync_ok: true,
};
