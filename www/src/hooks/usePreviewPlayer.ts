import { useCallback, useEffect, useRef } from 'react';
import type { MediaPlaybackUrls, MediaStreamRuntime, StreamName } from '../api/types';
import {
  buildPreviewModeState,
  previewModeLabels,
  type PreviewMode,
} from './previewMode';
import { usePreviewPlaybackSession } from './usePreviewPlaybackSession';

export { previewModeLabels, type PreviewMode } from './previewMode';

interface UsePreviewPlayerOptions {
  active?: MediaStreamRuntime;
  enabled: boolean;
  mode: PreviewMode;
  playbackUrls: MediaPlaybackUrls | null;
  setMode: (mode: PreviewMode) => void;
  stream: StreamName;
}

export function usePreviewPlayer({
  active,
  enabled,
  mode,
  playbackUrls,
  setMode,
  stream,
}: UsePreviewPlayerOptions) {
  const modeSelectionRef = useRef<'auto' | 'manual'>('auto');
  const autoModeSelected = modeSelectionRef.current === 'auto';
  const onAutoModeFallback = useCallback(() => {
    modeSelectionRef.current = 'auto';
  }, []);
  const modeState = buildPreviewModeState(
    active,
    mode,
    playbackUrls,
  );
  const {
    connected,
    decodedSize,
    displaySize,
    mediaLayers,
    previewState,
    retainedFrameVisible,
    restartPreview,
    visibleLayer,
  } = usePreviewPlaybackSession({
    enabled,
    mode,
    modeState,
    onAutoModeFallback,
    autoModeSelected,
    playbackUrls,
    setMode,
    stream,
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
    if (!enabled) {
      return;
    }

    if (modeSelectionRef.current === 'manual') {
      if (!modeState.selectedModeEnabled) {
        modeSelectionRef.current = 'auto';
        restartPreview(`${previewModeLabels[mode]} 暂不可用`);
        if (modeState.nextAutoMode && modeState.nextAutoMode !== mode) {
          setMode(modeState.nextAutoMode);
        }
      }
      return;
    }

    if (modeState.nextAutoMode && modeState.nextAutoMode !== mode) {
      restartPreview('正在切换预览链路');
      setMode(modeState.nextAutoMode);
    }
  }, [
    enabled,
    mode,
    modeState.nextAutoMode,
    modeState.selectedModeEnabled,
    restartPreview,
    setMode,
  ]);

  return {
    connected,
    decodedSize,
    displaySize,
    flvPlaybackEnabled: modeState.flvPlaybackReady,
    flvSupported: modeState.flvSupported,
    hlsLaunchable: modeState.hlsLaunchable,
    hlsSupported: modeState.hlsSupported,
    mediaLayers,
    mjpegPlaybackEnabled: modeState.mjpegPlaybackReady,
    mjpegSupported: modeState.mjpegSupported,
    previewState,
    retainedFrameVisible,
    restartPreview,
    streamRunning: modeState.streamRunning,
    switchMode,
    visibleLayer,
    webrtcEnabled: modeState.webrtcEnabled,
    webrtcPlaybackEnabled: modeState.webrtcPlaybackReady,
    webrtcSupported: modeState.webrtcSupported,
  };
}
