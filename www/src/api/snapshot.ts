import { mockSnapshotConfig } from './mockSnapshot';
import { authQuery, requestJson, putJson } from './client';
import type { SnapshotConfig } from './types/media/configuration';

export function getSnapshotConfig(): Promise<SnapshotConfig> {
    return requestJson<SnapshotConfig>(
        '/api/config/snapshot',
        mockSnapshotConfig,
    );
}

export function saveSnapshotConfig(value: SnapshotConfig): Promise<void> {
    return putJson('/api/config/snapshot', value);
}

export function snapshotUrl(stream: string, tick = 0): string {
    const query = authQuery({
        entries: tick > 0 ? { t: String(tick) } : undefined,
    });
    return `/snapshot/${stream}.jpg${query ? `?${query}` : ''}`;
}
