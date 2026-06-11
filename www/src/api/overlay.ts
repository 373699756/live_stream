import { mockOverlayConfig } from './mockOverlay';
import { requestJson, putJson } from './client';
import type { OverlayConfig } from './types';

export function getOverlayConfig(): Promise<OverlayConfig> {
    return requestJson<OverlayConfig>('/api/config/overlay', mockOverlayConfig);
}

export function saveOverlayConfig(value: OverlayConfig): Promise<void> {
    return putJson('/api/config/overlay', value);
}
