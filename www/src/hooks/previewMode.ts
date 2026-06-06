import type { StreamStatus, WebrtcConfig } from '../api/types';

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
  hlsModeEnabled: boolean;
  hlsPlaybackReady: boolean;
  hlsReady: boolean;
  hlsSupported: boolean;
  mjpegModeEnabled: boolean;
  mjpegPlaybackReady: boolean;
  mjpegReady: boolean;
  mjpegSupported: boolean;
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
  active: StreamStatus | undefined,
  mode: PreviewMode,
  webrtcConfig: WebrtcConfig | null,
  webrtcConfigLoaded: boolean,
): PreviewModeState {
  const hlsReady = active?.hlsReady ?? false;
  const flvReady = active?.flvReady ?? false;
  const mjpegReady = active?.mjpegReady ?? false;
  const webrtcReady = active?.webrtcReady ?? false;
  const hlsSupported = active?.hlsSupported ?? false;
  const flvSupported = active?.flvSupported ?? false;
  const mjpegSupported = active?.mjpegSupported ?? false;
  const webrtcSupported = webrtcReady;
  const webrtcEnabled = Boolean(webrtcConfig?.enabled);
  const streamRunning = active?.state === 'running';
  const webrtcModeEnabled =
    webrtcConfigLoaded && webrtcEnabled && webrtcSupported;
  const hlsModeEnabled = hlsSupported && streamRunning;
  const flvModeEnabled = flvSupported && streamRunning;
  const mjpegModeEnabled = mjpegSupported && streamRunning;
  const webrtcPlaybackReady = webrtcModeEnabled && webrtcReady;
  const hlsPlaybackReady = hlsModeEnabled && hlsReady;
  const flvPlaybackReady = flvModeEnabled && flvReady;
  const mjpegPlaybackReady = mjpegModeEnabled && mjpegReady;
  const selectedModeEnabled =
    (mode === 'webrtc' && webrtcModeEnabled) ||
    (mode === 'hls' && hlsModeEnabled) ||
    (mode === 'flv' && flvModeEnabled) ||
    (mode === 'mjpeg' && mjpegModeEnabled);
  const nextReadyMode =
    flvPlaybackReady ? 'flv' :
    webrtcPlaybackReady ? 'webrtc' :
    hlsPlaybackReady ? 'hls' :
    mjpegPlaybackReady ? 'mjpeg' :
    null;

  return {
    flvModeEnabled,
    flvPlaybackReady,
    flvReady,
    flvSupported,
    hlsModeEnabled,
    hlsPlaybackReady,
    hlsReady,
    hlsSupported,
    mjpegModeEnabled,
    mjpegPlaybackReady,
    mjpegReady,
    mjpegSupported,
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
