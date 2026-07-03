import type { StreamName } from '../api/types';
import { previewModeLabels, type PreviewMode } from '../hooks/previewMode';
import type { PreviewStreamSummary } from './previewDisplay';

interface PreviewToolbarProps {
    flvPreviewEnabled: boolean;
    flvSupported: boolean;
    hlsModeEnabled: boolean;
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
    hlsModeEnabled,
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
    const protocolDisabledText = (
        supported: boolean,
        ready: boolean,
        unsupportedText: string,
    ) => {
        if (!supported) {
            return unsupportedText;
        }
        if (!ready) {
            return '码流或协议尚未就绪';
        }
        return undefined;
    };
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
            <div className="preview-toolbar-group stream-toolbar-group">
                <span className="preview-toolbar-label">码流</span>
                <div className="stream-switcher">
                    {renderStreamButton('main', mainSummary)}
                    {renderStreamButton('sub', subSummary)}
                </div>
            </div>
            <div className="preview-toolbar-right">
                <div className="preview-toolbar-group">
                    <span className="preview-toolbar-label">协议</span>
                    <div className="preview-actions protocol-actions">
                        <button
                            type="button"
                            className={mode === 'webrtc' ? 'active' : ''}
                            disabled={
                                !webrtcEnabled ||
                                !webrtcSupported ||
                                !webrtcPreviewEnabled
                            }
                            title={protocolDisabledText(
                                webrtcSupported,
                                webrtcPreviewEnabled,
                                '当前编码或浏览器不支持 WebRTC 预览',
                            )}
                            onClick={() => onModeChange('webrtc')}
                        >
                            {previewModeLabels.webrtc}
                        </button>
                        <button
                            type="button"
                            className={mode === 'hls' ? 'active' : ''}
                            disabled={!hlsSupported || !hlsModeEnabled}
                            title={protocolDisabledText(
                                hlsSupported,
                                hlsModeEnabled,
                                '当前编码或浏览器不支持 HLS 预览',
                            )}
                            onClick={() => onModeChange('hls')}
                        >
                            {previewModeLabels.hls}
                        </button>
                        <button
                            type="button"
                            className={mode === 'flv' ? 'active' : ''}
                            disabled={!flvSupported || !flvPreviewEnabled}
                            title={protocolDisabledText(
                                flvSupported,
                                flvPreviewEnabled,
                                '当前编码不支持 HTTP-FLV 预览',
                            )}
                            onClick={() => onModeChange('flv')}
                        >
                            {previewModeLabels.flv}
                        </button>
                        <button
                            type="button"
                            className={mode === 'mjpeg' ? 'active' : ''}
                            disabled={!mjpegSupported || !mjpegPreviewEnabled}
                            title={protocolDisabledText(
                                mjpegSupported,
                                mjpegPreviewEnabled,
                                '当前编码不支持 MJPEG 预览',
                            )}
                            onClick={() => onModeChange('mjpeg')}
                        >
                            {previewModeLabels.mjpeg}
                        </button>
                    </div>
                </div>
                <div className="preview-actions preview-command-actions">
                    {onSnapshot && (
                        <button
                            type="button"
                            disabled={!streamRunning}
                            title={
                                streamRunning
                                    ? '打开当前码流抓图'
                                    : '码流未运行，无法抓图'
                            }
                            onClick={() => onSnapshot(stream)}
                        >
                            抓图
                        </button>
                    )}
                    <button
                        type="button"
                        title="进入或退出全屏预览"
                        onClick={onToggleFullscreen}
                    >
                        全屏
                    </button>
                </div>
            </div>
        </div>
    );
}
