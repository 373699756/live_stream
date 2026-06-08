import type { MediaPlaybackUrls, MediaStreamRuntime } from '../api/types';

export type PreviewMode = 'webrtc' | 'hls' | 'flv' | 'mjpeg';

export const previewModeLabels: Record<PreviewMode, string> = {
  webrtc: 'WebRTC',
  hls: 'HLS',
  flv: 'HTTP-FLV',
  mjpeg: 'MJPEG',
};

export interface PreviewModeState {
  flvModeEnabled: boolean;
  flvPlaybackReady: boolean;
  flvReady: boolean;
  flvSupported: boolean;
  hlsLaunchable: boolean;
  hlsModeEnabled: boolean;
  hlsPlaybackReady: boolean;
  hlsReady: boolean;
  hlsSupported: boolean;
  mjpegModeEnabled: boolean;
  mjpegPlaybackReady: boolean;
  mjpegReady: boolean;
  mjpegSupported: boolean;
  nextAutoMode: PreviewMode | null;
  nextReadyMode: PreviewMode | null;
  selectedModeEnabled: boolean;
  streamRunning: boolean;
  webrtcEnabled: boolean;
  webrtcModeEnabled: boolean;
  webrtcPlaybackReady: boolean;
  webrtcReady: boolean;
  webrtcSupported: boolean;
}

export function buildPreviewModeState(
  active: MediaStreamRuntime | undefined,
  mode: PreviewMode,
  playbackUrls: MediaPlaybackUrls | null,
): PreviewModeState {
  const hlsReady = active?.hls_ready ?? false;
  const flvReady = active?.http_flv_ready ?? false;
  const mjpegReady = active?.mjpeg_ready ?? false;
  const webrtcReady = active?.webrtc_ready ?? false;
  const hlsSupported = Boolean(active?.hls_supported && playbackUrls?.hls);
  const flvSupported = Boolean(active?.http_flv_supported && playbackUrls?.http_flv);
  const mjpegSupported = Boolean(active?.mjpeg_supported && playbackUrls?.mjpeg);
  const webrtcSupported = Boolean(active?.webrtc_supported);
  const webrtcEnabled = webrtcSupported;
  const streamRunning = active?.running ?? false;
  const webrtcModeEnabled = webrtcEnabled && streamRunning;
  const hlsLaunchable = hlsSupported && streamRunning;
  const hlsModeEnabled = hlsLaunchable;
  const flvModeEnabled = flvSupported && streamRunning;
  const mjpegModeEnabled = mjpegSupported && streamRunning;
  const webrtcPlaybackReady = webrtcModeEnabled && webrtcReady;
  const hlsPlaybackReady = hlsLaunchable && hlsReady;
  const flvPlaybackReady = flvModeEnabled && flvReady;
  const mjpegPlaybackReady = mjpegModeEnabled && mjpegReady;
  const selectedModeEnabled =
    (mode === 'webrtc' && webrtcPlaybackReady) ||
    (mode === 'hls' && hlsLaunchable) ||
    (mode === 'flv' && flvPlaybackReady) ||
    (mode === 'mjpeg' && mjpegPlaybackReady);
  const nextReadyMode =
    webrtcPlaybackReady ? 'webrtc' :
    flvPlaybackReady ? 'flv' :
    mjpegPlaybackReady ? 'mjpeg' :
    hlsPlaybackReady ? 'hls' :
    null;
  const nextAutoMode = nextReadyMode;

  return {
    flvModeEnabled,
    flvPlaybackReady,
    flvReady,
    flvSupported,
    hlsLaunchable,
    hlsModeEnabled,
    hlsPlaybackReady,
    hlsReady,
    hlsSupported,
    mjpegModeEnabled,
    mjpegPlaybackReady,
    mjpegReady,
    mjpegSupported,
    nextAutoMode,
    nextReadyMode,
    selectedModeEnabled,
    streamRunning,
    webrtcEnabled,
    webrtcModeEnabled,
    webrtcPlaybackReady,
    webrtcReady,
    webrtcSupported,
  };
}
