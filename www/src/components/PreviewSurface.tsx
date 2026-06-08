import type { ReactNode, RefObject } from 'react';
import type { PreviewMediaLayerRefs } from '../hooks/usePreviewPlaybackSession';

interface PreviewSurfaceProps {
  connected: boolean;
  enabled: boolean;
  mediaLayers: PreviewMediaLayerRefs[];
  onToggleFullscreen: () => void;
  previewDetail: string;
  previewState: string;
  surfaceOverlay?: ReactNode;
  surfaceRef: RefObject<HTMLDivElement | null>;
  visibleLayer: number;
}

export function PreviewSurface({
  connected,
  enabled,
  mediaLayers,
  onToggleFullscreen,
  previewDetail,
  previewState,
  surfaceOverlay,
  surfaceRef,
  visibleLayer,
}: PreviewSurfaceProps) {
  return (
    <div className="video-surface" ref={surfaceRef} onDoubleClick={onToggleFullscreen}>
      {!enabled ? (
        <div className="video-placeholder">
          <div className="lens-ring paused" />
          <strong>预览已暂停</strong>
          <span>正在应用视频参数</span>
        </div>
      ) : (
        mediaLayers.map((layer, index) => (
          <div
            className={`video-layer${index === visibleLayer ? ' active' : ''}`}
            key={index}
          >
            <video
              ref={layer.videoRef}
              className={layer.mediaKind === 'video' ? 'video-element' : 'video-element hidden'}
              autoPlay
              muted
              playsInline
            />
            <img
              ref={layer.imageRef}
              className={layer.mediaKind === 'mjpeg' ? 'video-element' : 'video-element hidden'}
              alt=""
            />
          </div>
        ))
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
