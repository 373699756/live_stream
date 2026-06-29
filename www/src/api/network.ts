// Network configuration API: /api/config/network

import { mockNetworkConfig } from './mockNetwork';
import { requestJson, putJson } from './client';
import type { NetworkConfig } from './types/media/configuration';

export function getNetworkConfig(): Promise<NetworkConfig> {
    return requestJson<NetworkConfig>('/api/config/network', mockNetworkConfig);
}

export function saveNetworkConfig(value: NetworkConfig): Promise<void> {
    return putJson('/api/config/network', value);
}
