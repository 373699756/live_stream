import { mockUpgradeStatus } from './mock';
import {
  postJson,
  authHeaders,
  readError,
  requestJson,
  useMockFallback,
} from './client';
import type {
  UpgradePackageInfo,
  UpgradeRequest,
  UpgradeStatus,
} from './types';

export function getUpgradeStatus(): Promise<UpgradeStatus> {
  return requestJson<UpgradeStatus>('/api/upgrade/status', mockUpgradeStatus);
}

function mockUpgradePackage(file: File): UpgradePackageInfo {
  const stem = file.name.replace(/\.[^.]+$/, '') || 'firmware';
  return {
    package_path: `/tmp/live_stream/upgrade/uploads/${file.name}`,
    version: stem,
    size_bytes: file.size,
    digest: 'mock-digest',
    build_time_ms: Date.now(),
    target_model: 'live_stream_ipc',
    requires_reboot: true,
  };
}

export async function uploadUpgradePackage(file: File): Promise<UpgradePackageInfo> {
  const response = await fetch(
    `/api/upgrade/upload?filename=${encodeURIComponent(file.name)}`,
    {
      method: 'POST',
      headers: authHeaders({
        headers: { 'Content-Type': 'application/octet-stream' },
      }),
      body: file,
    },
  );
  if (!response.ok) {
    if (useMockFallback) {
      return mockUpgradePackage(file);
    }
    throw new Error(await readError(response));
  }
  return (await response.json()) as UpgradePackageInfo;
}

export function startUpgrade(value: UpgradeRequest): Promise<UpgradeStatus> {
  return postJson<UpgradeRequest, UpgradeStatus>('/api/upgrade/start', value, {
    ...mockUpgradeStatus,
    state: 'validating',
    current_stage: 'validating',
    target_version: value.expected_version,
    started_at_ms: Date.now(),
  });
}

export function cancelUpgrade(): Promise<UpgradeStatus> {
  return postJson<Record<string, never>, UpgradeStatus>('/api/upgrade/cancel', {}, {
    ...mockUpgradeStatus,
    state: 'canceled',
    current_stage: 'canceled',
    error_message: 'canceled',
    finished_at_ms: Date.now(),
  });
}

export function confirmUpgradeReboot(): Promise<UpgradeStatus> {
  return postJson<Record<string, never>, UpgradeStatus>(
    '/api/upgrade/confirm-reboot',
    {},
    {
      ...mockUpgradeStatus,
      state: 'completed',
      current_stage: 'completed',
      progress_percent: 100,
      finished_at_ms: Date.now(),
    },
  );
}
