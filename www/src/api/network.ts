// Network configuration API: /api/config/network

import { mockNetworkConfig } from './mock';
import { requestJson, putJson } from './client';
import type { NetworkConfig } from './types';

export function getNetworkConfig(): Promise<NetworkConfig> {
  return requestJson<NetworkConfig>('/api/config/network', mockNetworkConfig);
}

export function saveNetworkConfig(value: NetworkConfig): Promise<void> {
  return putJson('/api/config/network', value);
}
