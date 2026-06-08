import { type ReactNode, useRef, useState } from 'react';
import type {
  MediaPlaybackUrls,
  MediaStreamRuntime,
  StreamName,
} from '../api/types';
import { usePreviewPlayer, type PreviewMode } from '../hooks/usePreviewPlayer';
import { PreviewFooter } from './PreviewFooter';
import { PreviewSurface } from './PreviewSurface';
import { PreviewToolbar } from './PreviewToolbar';
import { previewDetailText, previewStreamSummary } from './previewDisplay';

interface VideoPreviewProps {
  stream: StreamName;
  statuses: MediaStreamRuntime[];
  playbackUrls: MediaPlaybackUrls | null;
  onStreamChange: (stream: StreamName) => void;
  enabled?: boolean;
  onSnapshot?: (stream: StreamName) => void;
  surfaceOverlay?: ReactNode;
}

export function VideoPreview({
  stream,
  statuses,
  playbackUrls,
  onStreamChange,
  enabled = true,
  onSnapshot,
  surfaceOverlay,
}: VideoPreviewProps) {
  const [mode, setMode] = useState<PreviewMode>('webrtc');
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const active = statuses.find((item) => item.stream === stream);
  const {
    connected,
    decodedSize,
    displaySize,
    flvPlaybackEnabled,
    flvSupported,
    hlsPlaybackEnabled,
    hlsSupported,
    imageRef,
    isMjpegMode,
    mjpegPlaybackEnabled,
    mjpegSupported,
    previewState,
    restartPreview,
    streamRunning,
    switchMode,
    videoRef,
    webrtcEnabled,
    webrtcPlaybackEnabled,
    webrtcSupported,
  } = usePreviewPlayer({
    active,
    enabled,
    mode,
    playbackUrls,
    setMode,
    stream,
  });
  const switchStream = (nextStream: StreamName) => {
    if (nextStream === stream) {
      return;
    }
    restartPreview('正在切换码流');
    onStreamChange(nextStream);
  };

  const toggleFullscreen = () => {
    const surface = surfaceRef.current;
    if (!surface) {
      return;
    }
    if (document.fullscreenElement === surface) {
      void document.exitFullscreen?.();
      return;
    }
    void surface.requestFullscreen?.();
  };

  const mainSummary = previewStreamSummary(statuses, 'main');
  const subSummary = previewStreamSummary(statuses, 'sub');

  return (
    <section className="preview-panel">
      <PreviewToolbar
        flvPlaybackEnabled={flvPlaybackEnabled}
        flvSupported={flvSupported}
        hlsPlaybackEnabled={hlsPlaybackEnabled}
        hlsSupported={hlsSupported}
        mainSummary={mainSummary}
        mjpegPlaybackEnabled={mjpegPlaybackEnabled}
        mjpegSupported={mjpegSupported}
        mode={mode}
        onModeChange={switchMode}
        onSnapshot={onSnapshot}
        onStreamChange={switchStream}
        onToggleFullscreen={toggleFullscreen}
        stream={stream}
        streamRunning={streamRunning}
        subSummary={subSummary}
        webrtcEnabled={webrtcEnabled}
        webrtcPlaybackEnabled={webrtcPlaybackEnabled}
        webrtcSupported={webrtcSupported}
      />
      <PreviewSurface
        connected={connected}
        enabled={enabled}
        imageRef={imageRef}
        isMjpegMode={isMjpegMode}
        onToggleFullscreen={toggleFullscreen}
        previewDetail={previewDetailText(stream, mode)}
        previewState={previewState}
        surfaceOverlay={surfaceOverlay}
        surfaceRef={surfaceRef}
        videoRef={videoRef}
      />
      <PreviewFooter
        active={active}
        decodedSize={decodedSize}
        displaySize={displaySize}
        mode={mode}
        stream={stream}
      />
    </section>
  );
}
