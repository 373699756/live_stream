import type { VideoResolutionCapability, VideoStreamCapabilities, VideoStreamConfig } from './types';

export function resolutionValue(resolution: VideoResolutionCapability): string {
  return `${resolution.width}x${resolution.height}`;
}

export function resolutionLabel(resolution: VideoResolutionCapability): string {
  const value = resolutionValue(resolution);
  if (value === '3840x2160') return '3840 x 2160 (4K)';
  if (value === '2560x1440') return '2560 x 1440 (2K)';
  if (value === '1920x1080') return '1920 x 1080 (1080P)';
  if (value === '1280x720') return '1280 x 720 (720P)';
  if (value === '640x360') return '640 x 360 (360P)';
  return `${resolution.width} x ${resolution.height}`;
}

export function isValidResolution(value: string): boolean {
  return /^[1-9]\d{1,4}x[1-9]\d{1,4}$/.test(value);
}

export function isResolutionSupported(stream: VideoStreamConfig, capabilities: VideoStreamCapabilities): boolean {
  return capabilities.resolutions.some((item) => resolutionValue(item) === stream.resolution);
}

export function isStreamSupported(stream: VideoStreamConfig, capabilities: VideoStreamCapabilities): boolean {
  return (
    capabilities.available !== false &&
    capabilities.codecs.some((item) => item.codec === stream.codec) &&
    isResolutionSupported(stream, capabilities) &&
    stream.fps >= capabilities.fps.min &&
    stream.fps <= capabilities.fps.max &&
    stream.bitrate_kbps >= capabilities.bitrate_kbps.min &&
    stream.bitrate_kbps <= capabilities.bitrate_kbps.max &&
    capabilities.rate_control.includes(stream.rate_control) &&
    stream.gop >= capabilities.gop.min &&
    stream.gop <= capabilities.gop.max
  );
}
