// Image / ISP API: /api/config/image

import { mockImageConfig, mockImageStrategyStatus } from './mockImage';
import { requestJson, putJson, type ApiRequestOptions } from './client';
import type { ImageConfig, ImageStrategyStatus } from './types';

function normalizeImageConfig(config: ImageConfig): ImageConfig {
  const next: ImageConfig = {
    ...config,
    exposure: { ...config.exposure },
    backlight: { ...config.backlight },
    color_mode: { ...config.color_mode },
    strategy: config.strategy
      ? { ...config.strategy }
      : { enabled: true, mode: 'balanced' },
  };
  if (!next.color_mode.mode) {
    next.color_mode.mode = 'color';
  }
  if (!next.strategy?.mode) {
    next.strategy = { enabled: next.strategy?.enabled ?? true, mode: 'balanced' };
  }
  return next;
}

export function getImageConfig(
  options?: ApiRequestOptions,
): Promise<ImageConfig> {
  return requestJson<ImageConfig>('/api/config/image', mockImageConfig, options)
    .then(normalizeImageConfig);
}

export function saveImageConfig(
  value: ImageConfig,
  options?: ApiRequestOptions,
): Promise<void> {
  return putJson('/api/config/image', value, options);
}

export function getImageStrategyStatus(
  options?: ApiRequestOptions,
): Promise<ImageStrategyStatus> {
  return requestJson<ImageStrategyStatus>(
    '/api/status/image-strategy',
    mockImageStrategyStatus,
    options,
  );
}
