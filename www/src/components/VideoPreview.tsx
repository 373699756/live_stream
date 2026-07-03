import { type ReactNode, useRef, useState } from 'react';
import type {
    MediaPreviewUrls,
    MediaStreamInfo,
    StreamName,
    WebrtcConfig,
} from '../api/types';
import { usePreviewPlayer, type PreviewMode } from '../hooks/usePreviewPlayer';
import { PreviewFooter } from './PreviewFooter';
import { PreviewSurface } from './PreviewSurface';
import { PreviewToolbar } from './PreviewToolbar';
import { previewDetailText, previewStreamSummary } from './previewDisplay';

interface VideoPreviewProps {
    stream: StreamName;
    statuses: MediaStreamInfo[];
    previewUrls: MediaPreviewUrls | null;
    onStreamChange: (stream: StreamName) => void;
    enabled?: boolean;
    fit?: 'contain' | 'cover';
    onSnapshot?: (stream: StreamName) => void;
    surfaceOverlay?: ReactNode;
    webrtcConfig?: WebrtcConfig | null;
}

export function VideoPreview({
    stream,
    statuses,
    previewUrls,
    onStreamChange,
    enabled = true,
    fit = 'contain',
    onSnapshot,
    surfaceOverlay,
    webrtcConfig = null,
}: VideoPreviewProps) {
    const [mode, setMode] = useState<PreviewMode>('webrtc');
    const surfaceRef = useRef<HTMLDivElement | null>(null);
    const active = statuses.find((item) => item.stream === stream);
    const activePreviewUrls =
        previewUrls?.stream === stream ? previewUrls : null;
    const {
        connected,
        decodedSize,
        displaySize,
        flvPreviewEnabled,
        flvSupported,
        hlsModeEnabled,
        hlsSupported,
        mediaLayers,
        mjpegPreviewEnabled,
        mjpegSupported,
        previewState,
        retainedFrameVisible,
        restartPreview,
        streamRunning,
        switchMode,
        visibleLayer,
        webrtcEnabled,
        webrtcPreviewEnabled,
        webrtcSupported,
    } = usePreviewPlayer({
        active,
        enabled,
        mode,
        previewUrls: activePreviewUrls,
        setMode,
        stream,
        webrtcConfig,
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
        <section className={`preview-panel preview-fit-${fit}`}>
            <PreviewToolbar
                flvPreviewEnabled={flvPreviewEnabled}
                flvSupported={flvSupported}
                hlsModeEnabled={hlsModeEnabled}
                hlsSupported={hlsSupported}
                mainSummary={mainSummary}
                mjpegPreviewEnabled={mjpegPreviewEnabled}
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
                webrtcPreviewEnabled={webrtcPreviewEnabled}
                webrtcSupported={webrtcSupported}
            />
            <PreviewSurface
                connected={connected}
                enabled={enabled}
                fit={fit}
                mediaLayers={mediaLayers}
                onToggleFullscreen={toggleFullscreen}
                previewDetail={previewDetailText(stream, mode)}
                previewState={previewState}
                retainedFrameVisible={retainedFrameVisible}
                surfaceOverlay={surfaceOverlay}
                surfaceRef={surfaceRef}
                visibleLayer={visibleLayer}
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
