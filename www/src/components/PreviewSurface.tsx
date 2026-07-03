import type { ReactNode, RefObject } from 'react';
import type { PreviewMediaLayerRefs } from './previewSurfaceTypes';

interface PreviewSurfaceProps {
    connected: boolean;
    enabled: boolean;
    fit?: 'contain' | 'cover';
    mediaLayers: PreviewMediaLayerRefs[];
    onToggleFullscreen: () => void;
    previewDetail: string;
    previewState: string;
    retainedFrameVisible: boolean;
    surfaceOverlay?: ReactNode;
    surfaceRef: RefObject<HTMLDivElement | null>;
    visibleLayer: number;
}

export function PreviewSurface({
    connected,
    enabled,
    fit = 'contain',
    mediaLayers,
    onToggleFullscreen,
    previewDetail,
    previewState,
    retainedFrameVisible,
    surfaceOverlay,
    surfaceRef,
    visibleLayer,
}: PreviewSurfaceProps) {
    // 保留帧只是视觉过渡，状态浮层仍要显示目标播放会话的真实状态。
    const placeholderClass = retainedFrameVisible
        ? 'video-placeholder retained-frame-status'
        : 'video-placeholder';

    return (
        <div
            className={`video-surface video-fit-${fit}`}
            ref={surfaceRef}
            onDoubleClick={onToggleFullscreen}
        >
            {!enabled ? (
                <div className="video-placeholder">
                    <div className="lens-ring paused" />
                    <strong>预览已暂停</strong>
                    <span>正在应用视频参数</span>
                </div>
            ) : (
                // 两个媒体层始终同时挂载，避免切协议时反复创建页面节点造成闪烁。
                mediaLayers.map((layer, index) => (
                    <div
                        className={`video-layer${index === visibleLayer ? ' active' : ''}`}
                        key={index}
                    >
                        <video
                            ref={layer.videoRef}
                            className={
                                layer.mediaKind === 'video'
                                    ? 'video-element'
                                    : 'video-element hidden'
                            }
                            autoPlay
                            muted
                            playsInline
                        />
                        <img
                            ref={layer.imageRef}
                            className={
                                layer.mediaKind === 'mjpeg'
                                    ? 'video-element'
                                    : 'video-element hidden'
                            }
                            alt=""
                        />
                    </div>
                ))
            )}
            {enabled && !connected && (
                <div className={placeholderClass}>
                    <div className="lens-ring" />
                    <strong>{previewState}</strong>
                    <span>{previewDetail}</span>
                </div>
            )}
            {surfaceOverlay}
        </div>
    );
}
