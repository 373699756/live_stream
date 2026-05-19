// Video & media API: /api/config/video, /api/media/capabilities, /api/status/streams

import { mockMediaCapabilities, mockVideoConfig } from './mock';
import { mockStreamStatus } from './mock';
import { requestJson, putJson, type ApiRequestOptions } from './client';
import type { MediaCapabilities, StreamStatus, VideoConfig } from './types';

export function getVideoConfig(
  options?: ApiRequestOptions,
): Promise<VideoConfig> {
  return requestJson<VideoConfig>('/api/config/video', mockVideoConfig, options);
}

export function saveVideoConfig(
  value: VideoConfig,
  options?: ApiRequestOptions,
): Promise<void> {
  return putJson('/api/config/video', value, options);
}

export function getMediaCapabilities(
  options?: ApiRequestOptions,
): Promise<MediaCapabilities> {
  return requestJson<MediaCapabilities>(
    '/api/media/capabilities',
    mockMediaCapabilities,
    options,
  );
}

export function getStreamStatus(
  options?: ApiRequestOptions,
): Promise<StreamStatus[]> {
  return requestJson<StreamStatus[]>(
    '/api/status/streams',
    mockStreamStatus,
    options,
  );
}
