// Time configuration and browser-assisted sync API.

import { mockTimeInfo } from './mockTime';
import {
    postJson,
    putJson,
    requestJson,
    type ApiRequestOptions,
} from './client';
import type {
    BrowserSyncConfig,
    NtpConfig,
    TimeConfig,
    TimeInfo,
} from './types';

const timeSyncTimeoutMs = 5000;

function normalizeTimeInfo(timeInfo: TimeInfo): TimeInfo {
    return {
        ...mockTimeInfo,
        ...timeInfo,
        ntp: {
            ...mockTimeInfo.ntp,
            ...(timeInfo.ntp ?? {}),
            servers: Array.isArray(timeInfo.ntp?.servers)
                ? timeInfo.ntp.servers
                : [],
        },
        manual_sync_allowed:
            typeof timeInfo.manual_sync_allowed === 'boolean'
                ? timeInfo.manual_sync_allowed
                : true,
        browser_sync_on_login:
            typeof timeInfo.browser_sync_on_login === 'boolean'
                ? timeInfo.browser_sync_on_login
                : true,
    };
}

export function getTimeInfo(init?: ApiRequestOptions): Promise<TimeInfo> {
    return requestJson<TimeInfo>(
        '/api/system/time/status',
        mockTimeInfo,
        init,
    ).then(normalizeTimeInfo);
}

export function saveTimezone(
    timezone: string,
    init?: ApiRequestOptions,
): Promise<void> {
    return putJson('/api/system/time/timezone', { timezone }, init);
}

export function saveTimeConfig(
    config: TimeConfig,
    init?: ApiRequestOptions,
): Promise<void> {
    return putJson('/api/system/time/config', config, {
        timeoutMs: timeSyncTimeoutMs,
        ...init,
    });
}

export function saveNtpConfig(
    ntp: NtpConfig,
    init?: ApiRequestOptions,
): Promise<void> {
    return putJson('/api/system/time/ntp', ntp, init);
}

export function saveBrowserSyncConfig(
    config: BrowserSyncConfig,
    init?: ApiRequestOptions,
): Promise<void> {
    return putJson('/api/system/time/browser-sync', config, init);
}

export function syncNtpNow(init?: ApiRequestOptions): Promise<void> {
    return postJson<Record<string, never>, void>(
        '/api/system/time/sync',
        {},
        undefined,
        init,
    );
}

export function syncBrowserTime(init?: ApiRequestOptions): Promise<void> {
    return postJson<{ system_time_ms: number }, void>(
        '/api/system/time/browser-time',
        { system_time_ms: Date.now() },
        undefined,
        { timeoutMs: timeSyncTimeoutMs, ...init },
    );
}
