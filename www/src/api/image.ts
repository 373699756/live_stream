// Image / ISP API: /api/config/image

import { mockImageConfig } from './mock';
import { requestJson, putJson } from './client';
import type { ImageConfig } from './types';

export function getImageConfig(): Promise<ImageConfig> {
  return requestJson<ImageConfig>('/api/config/image', mockImageConfig);
}

export function saveImageConfig(value: ImageConfig): Promise<boolean> {
  return putJson('/api/config/image', value);
}
