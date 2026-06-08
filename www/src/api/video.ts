// Video & media API: /api/config/video and /api/media/capabilities

import {
  mockMediaCapabilities,
  mockVideoConfig,
} from './mockVideo';
import { requestJson, putJson, type ApiRequestOptions } from './client';
import { codecSupportsSmartP } from './resolution';
import type { MediaCapabilities, VideoConfig, VideoStreamConfig } from './types';

function normalizeStreamConfig(stream: VideoStreamConfig): VideoStreamConfig {
  const next: VideoStreamConfig = {
    ...stream,
    gop_mode: stream.gop_mode ?? 'normal_p',
    smart_codec: stream.smart_codec ?? false,
  };
  if (!codecSupportsSmartP(next.codec)) {
    next.smart_codec = false;
    if (next.gop_mode === 'smart_p') {
      next.gop_mode = 'normal_p';
    }
  }
  return next;
}

function normalizeVideoConfig(config: VideoConfig): VideoConfig {
  return {
    streams: {
      main: normalizeStreamConfig(config.streams.main),
      sub: normalizeStreamConfig(config.streams.sub),
    },
  };
}

export function getVideoConfig(
  options?: ApiRequestOptions,
): Promise<VideoConfig> {
  return requestJson<VideoConfig>('/api/config/video', mockVideoConfig, options)
    .then(normalizeVideoConfig);
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
