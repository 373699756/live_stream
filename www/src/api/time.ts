// Time configuration and browser-assisted sync API.

import { mockTimeStatus } from './mockTime';
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
  TimeStatus,
} from './types';

const timeSyncTimeoutMs = 5000;

function normalizeTimeStatus(status: TimeStatus): TimeStatus {
  return {
    ...mockTimeStatus,
    ...status,
    ntp: {
      ...mockTimeStatus.ntp,
      ...(status.ntp ?? {}),
      servers: Array.isArray(status.ntp?.servers) ? status.ntp.servers : [],
    },
    manual_sync_allowed:
      typeof status.manual_sync_allowed === 'boolean'
        ? status.manual_sync_allowed
        : true,
    browser_sync_on_login:
      typeof status.browser_sync_on_login === 'boolean'
        ? status.browser_sync_on_login
        : true,
  };
}

export function getTimeStatus(
  init?: ApiRequestOptions,
): Promise<TimeStatus> {
  return requestJson<TimeStatus>(
    '/api/system/time/status',
    mockTimeStatus,
    init,
  ).then(normalizeTimeStatus);
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
  return putJson('/api/system/time/config', config, init);
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

export function syncBrowserTime(
  init?: ApiRequestOptions,
): Promise<void> {
  return postJson<{ system_time_ms: number }, void>(
    '/api/system/time/browser-time',
    { system_time_ms: Date.now() },
    undefined,
    { timeoutMs: timeSyncTimeoutMs, ...init },
  );
}
