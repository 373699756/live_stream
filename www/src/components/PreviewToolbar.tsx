import type { StreamName } from '../api/types';
import { previewModeLabels, type PreviewMode } from '../hooks/previewMode';
import type { PreviewStreamSummary } from './previewDisplay';

interface PreviewToolbarProps {
    flvPreviewEnabled: boolean;
    flvSupported: boolean;
    hlsLaunchable: boolean;
    hlsSupported: boolean;
    mainSummary: PreviewStreamSummary;
    mjpegPreviewEnabled: boolean;
    mjpegSupported: boolean;
    mode: PreviewMode;
    onModeChange: (mode: PreviewMode) => void;
    onSnapshot?: (stream: StreamName) => void;
    onStreamChange: (stream: StreamName) => void;
    onToggleFullscreen: () => void;
    stream: StreamName;
    streamRunning: boolean;
    subSummary: PreviewStreamSummary;
    webrtcEnabled: boolean;
    webrtcPreviewEnabled: boolean;
    webrtcSupported: boolean;
}

export function PreviewToolbar({
    flvPreviewEnabled,
    flvSupported,
    hlsLaunchable,
    hlsSupported,
    mainSummary,
    mjpegPreviewEnabled,
    mjpegSupported,
    mode,
    onModeChange,
    onSnapshot,
    onStreamChange,
    onToggleFullscreen,
    stream,
    streamRunning,
    subSummary,
    webrtcEnabled,
    webrtcPreviewEnabled,
    webrtcSupported,
}: PreviewToolbarProps) {
    const renderStreamButton = (
        name: StreamName,
        summary: PreviewStreamSummary,
    ) => (
        <button
            type="button"
            className={stream === name ? 'active' : ''}
            onClick={() => onStreamChange(name)}
        >
            <strong>{summary.label}</strong>
            <span className={summary.running ? 'running' : ''}>
                {summary.state}
            </span>
            <em>{summary.detail}</em>
        </button>
    );

    return (
        <div className="preview-toolbar">
            <div className="stream-switcher">
                {renderStreamButton('main', mainSummary)}
                {renderStreamButton('sub', subSummary)}
            </div>
            <div className="preview-actions">
                <button
                    type="button"
                    className={mode === 'webrtc' ? 'active' : ''}
                    disabled={
                        !webrtcEnabled ||
                        !webrtcSupported ||
                        !webrtcPreviewEnabled
                    }
                    title={
                        !webrtcSupported
                            ? '当前编码不支持 WebRTC 预览'
                            : undefined
                    }
                    onClick={() => onModeChange('webrtc')}
                >
                    {previewModeLabels.webrtc}
                </button>
                <button
                    type="button"
                    className={mode === 'hls' ? 'active' : ''}
                    disabled={!hlsSupported || !hlsLaunchable}
                    title={
                        !hlsSupported ? '当前编码不支持 HLS 预览' : undefined
                    }
                    onClick={() => onModeChange('hls')}
                >
                    {previewModeLabels.hls}
                </button>
                <button
                    type="button"
                    className={mode === 'flv' ? 'active' : ''}
                    disabled={!flvSupported || !flvPreviewEnabled}
                    title={
                        !flvSupported
                            ? '当前编码不支持 HTTP-FLV 预览'
                            : undefined
                    }
                    onClick={() => onModeChange('flv')}
                >
                    {previewModeLabels.flv}
                </button>
                <button
                    type="button"
                    className={mode === 'mjpeg' ? 'active' : ''}
                    disabled={!mjpegSupported || !mjpegPreviewEnabled}
                    title={
                        !mjpegSupported
                            ? '当前编码不支持 MJPEG 预览'
                            : undefined
                    }
                    onClick={() => onModeChange('mjpeg')}
                >
                    {previewModeLabels.mjpeg}
                </button>
                {onSnapshot && (
                    <button
                        type="button"
                        disabled={!streamRunning}
                        onClick={() => onSnapshot(stream)}
                    >
                        抓图
                    </button>
                )}
                <button type="button" onClick={onToggleFullscreen}>
                    全屏
                </button>
            </div>
        </div>
    );
}
