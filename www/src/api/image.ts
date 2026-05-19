// Image / ISP API: /api/config/image

import { mockImageConfig } from './mock';
import { requestJson, putJson, type ApiRequestOptions } from './client';
import type { ImageConfig } from './types';

export function getImageConfig(
  options?: ApiRequestOptions,
): Promise<ImageConfig> {
  return requestJson<ImageConfig>('/api/config/image', mockImageConfig, options);
}

export function saveImageConfig(
  value: ImageConfig,
  options?: ApiRequestOptions,
): Promise<void> {
  return putJson('/api/config/image', value, options);
}
