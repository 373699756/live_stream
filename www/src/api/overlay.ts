import { mockOverlayConfig } from './mockOverlay';
import { requestJson, putJson } from './client';
import type { OverlayConfig } from './types/media/configuration';

export function getOverlayConfig(): Promise<OverlayConfig> {
    return requestJson<OverlayConfig>('/api/config/overlay', mockOverlayConfig);
}

export function saveOverlayConfig(value: OverlayConfig): Promise<void> {
    return putJson('/api/config/overlay', value);
}
