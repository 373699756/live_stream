// Video & media API: /api/config/video, /api/media/capabilities, /api/status/streams

import { mockMediaCapabilities, mockVideoConfig } from './mock';
import { mockStreamStatus } from './mock';
import { requestJson, putJson } from './client';
import type { MediaCapabilities, StreamStatus, VideoConfig } from './types';

export function getVideoConfig(): Promise<VideoConfig> {
  return requestJson<VideoConfig>('/api/config/video', mockVideoConfig);
}

export function saveVideoConfig(value: VideoConfig): Promise<boolean> {
  return putJson('/api/config/video', value);
}

export function getMediaCapabilities(): Promise<MediaCapabilities> {
  return requestJson<MediaCapabilities>('/api/media/capabilities', mockMediaCapabilities);
}

export function getStreamStatus(): Promise<StreamStatus[]> {
  return requestJson<StreamStatus[]>('/api/status/streams', mockStreamStatus);
}
