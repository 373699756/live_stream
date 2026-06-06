import { useCallback, useEffect, useRef } from 'react';
import type { StreamName, StreamStatus } from '../api/types';
import {
  buildPreviewModeState,
  previewModeLabels,
  type PreviewMode,
} from './previewMode';
import { usePreviewPlaybackSession } from './usePreviewPlaybackSession';
import { usePreviewWebrtcConfig } from './usePreviewWebrtcConfig';

export { previewModeLabels, type PreviewMode } from './previewMode';

interface UsePreviewPlayerOptions {
  active?: StreamStatus;
  enabled: boolean;
  mode: PreviewMode;
  setMode: (mode: PreviewMode) => void;
  stream: StreamName;
}

export function usePreviewPlayer({
  active,
  enabled,
  mode,
  setMode,
  stream,
}: UsePreviewPlayerOptions) {
  const modeSelectionRef = useRef<'auto' | 'manual'>('auto');
  const {
    config: webrtcConfig,
    loaded: webrtcConfigLoaded,
    error: webrtcConfigError,
  } = usePreviewWebrtcConfig();
  const onAutoModeFallback = useCallback(() => {
    modeSelectionRef.current = 'auto';
  }, []);
  const modeState = buildPreviewModeState(
    active,
    mode,
    webrtcConfig,
    webrtcConfigLoaded,
  );
  const {
    connected,
    decodedSize,
    displaySize,
    imageRef,
    previewState,
    restartPreview,
    videoRef,
  } = usePreviewPlaybackSession({
    enabled,
    mode,
    modeState,
    onAutoModeFallback,
    setMode,
    stream,
    webrtcConfig,
    webrtcConfigError,
    webrtcConfigLoaded,
  });

  const switchMode = useCallback((nextMode: PreviewMode) => {
    if (nextMode === mode) {
      return;
    }
    modeSelectionRef.current = 'manual';
    restartPreview('正在切换预览链路');
    setMode(nextMode);
  }, [mode, restartPreview, setMode]);

  useEffect(() => {
    if (!enabled || (mode === 'webrtc' && !webrtcConfigLoaded)) {
      return;
    }

    if (modeSelectionRef.current === 'manual') {
      if (!modeState.selectedModeEnabled) {
        modeSelectionRef.current = 'auto';
        restartPreview(`${previewModeLabels[mode]} 暂不可用`);
        if (modeState.nextReadyMode && modeState.nextReadyMode !== mode) {
          setMode(modeState.nextReadyMode);
        }
      }
      return;
    }

    if (modeState.nextReadyMode && modeState.nextReadyMode !== mode) {
      restartPreview('正在切换预览链路');
      setMode(modeState.nextReadyMode);
    }
  }, [
    enabled,
    mode,
    modeState.nextReadyMode,
    modeState.selectedModeEnabled,
    restartPreview,
    setMode,
    webrtcConfigLoaded,
  ]);

  return {
    connected,
    decodedSize,
    displaySize,
    flvPlaybackEnabled: modeState.flvPlaybackReady,
    flvSupported: modeState.flvSupported,
    hlsPlaybackEnabled: modeState.hlsPlaybackReady,
    hlsSupported: modeState.hlsSupported,
    imageRef,
    isMjpegMode: mode === 'mjpeg',
    mjpegPlaybackEnabled: modeState.mjpegPlaybackReady,
    mjpegSupported: modeState.mjpegSupported,
    previewState,
    restartPreview,
    streamRunning: modeState.streamRunning,
    switchMode,
    videoRef,
    webrtcEnabled: modeState.webrtcEnabled,
    webrtcPlaybackEnabled: modeState.webrtcPlaybackReady,
    webrtcSupported: modeState.webrtcSupported,
  };
}
