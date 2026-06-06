import type { ReactNode, RefObject } from 'react';

interface PreviewSurfaceProps {
  connected: boolean;
  enabled: boolean;
  imageRef: RefObject<HTMLImageElement | null>;
  isMjpegMode: boolean;
  onToggleFullscreen: () => void;
  previewDetail: string;
  previewState: string;
  surfaceOverlay?: ReactNode;
  surfaceRef: RefObject<HTMLDivElement | null>;
  videoRef: RefObject<HTMLVideoElement | null>;
}

export function PreviewSurface({
  connected,
  enabled,
  imageRef,
  isMjpegMode,
  onToggleFullscreen,
  previewDetail,
  previewState,
  surfaceOverlay,
  surfaceRef,
  videoRef,
}: PreviewSurfaceProps) {
  return (
    <div className="video-surface" ref={surfaceRef} onDoubleClick={onToggleFullscreen}>
      {!enabled ? (
        <div className="video-placeholder">
          <div className="lens-ring paused" />
          <strong>预览已暂停</strong>
          <span>正在应用视频参数</span>
        </div>
      ) : isMjpegMode ? (
        <img ref={imageRef} className="video-element" alt="" />
      ) : (
        <video ref={videoRef} className="video-element" autoPlay muted playsInline />
      )}
      {!connected && (
        <div className="video-placeholder">
          <div className="lens-ring" />
          <strong>{previewState}</strong>
          <span>{previewDetail}</span>
        </div>
      )}
      {surfaceOverlay}
    </div>
  );
}
