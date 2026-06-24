import { mockUpgradeInfo } from './mockUpgrade';
import {
    postJson,
    requestJson,
    uploadBinary,
    type ApiRequestOptions,
} from './client';
import type {
    UpgradePackageInfo,
    UpgradeRequest,
    UpgradeInfo,
} from './types';

export function getUpgradeInfo(
    init?: ApiRequestOptions,
): Promise<UpgradeInfo> {
    return requestJson<UpgradeInfo>(
        '/api/upgrade/status',
        mockUpgradeInfo,
        init,
    );
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

export async function uploadUpgradePackage(
    file: File,
): Promise<UpgradePackageInfo> {
    return uploadBinary<UpgradePackageInfo>({
        body: file,
        fallback: mockUpgradePackage(file),
        path: `/api/upgrade/upload?filename=${encodeURIComponent(file.name)}`,
    });
}

export function startUpgrade(value: UpgradeRequest): Promise<UpgradeInfo> {
    return postJson<UpgradeRequest, UpgradeInfo>(
        '/api/upgrade/start',
        value,
        {
            ...mockUpgradeInfo,
            state: 'validating',
            current_stage: 'validating',
            target_version: value.expected_version,
            started_at_ms: Date.now(),
        },
    );
}

export function cancelUpgrade(): Promise<UpgradeInfo> {
    return postJson<Record<string, never>, UpgradeInfo>(
        '/api/upgrade/cancel',
        {},
        {
            ...mockUpgradeInfo,
            state: 'canceled',
            current_stage: 'canceled',
            error_message: 'canceled',
            finished_at_ms: Date.now(),
        },
    );
}

export function confirmUpgradeReboot(): Promise<UpgradeInfo> {
    return postJson<Record<string, never>, UpgradeInfo>(
        '/api/upgrade/confirm-reboot',
        {},
        {
            ...mockUpgradeInfo,
            state: 'completed',
            current_stage: 'completed',
            progress_percent: 100,
            finished_at_ms: Date.now(),
        },
    );
}
