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

type ToolbarIconName =
    | 'camera'
    | 'flv'
    | 'fullscreen'
    | 'hls'
    | 'mjpeg'
    | 'webrtc';

function ToolbarIcon({ name }: { name: ToolbarIconName }) {
    const paths: Record<ToolbarIconName, string[]> = {
        camera: [
            'M5 8.5h3l1.5-2h5l1.5 2h3v9H5z',
            'M12 15.5a3 3 0 1 0 0-6 3 3 0 0 0 0 6z',
        ],
        flv: ['M13 3 6 13h5l-1 8 7-11h-5z'],
        fullscreen: [
            'M7 9V6h3',
            'M17 9V6h-3',
            'M7 15v3h3',
            'M17 15v3h-3',
        ],
        hls: [
            'M6 7h7',
            'M6 12h12',
            'M6 17h9',
            'M16 6l3 3-3 3',
        ],
        mjpeg: [
            'M5 6h14v12H5z',
            'M8 14l2.2-2.2 2.3 2.7 1.8-1.8L18 17',
            'M8.5 9.5h.01',
        ],
        webrtc: [
            'M12 17.5h.01',
            'M8.5 14a5 5 0 0 1 7 0',
            'M5.5 10.5a9 9 0 0 1 13 0',
            'M3 7a13 13 0 0 1 18 0',
        ],
    };
    return (
        <svg
            aria-hidden="true"
            className="preview-toolbar-icon"
            fill="none"
            viewBox="0 0 24 24"
        >
            {paths[name].map((path, index) => (
                <path d={path} key={index} />
            ))}
        </svg>
    );
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
            title={`${summary.label} ${summary.state} ${summary.detail}`}
            onClick={() => onStreamChange(name)}
        >
            <span className={summary.running ? 'running' : ''} />
            <strong>{summary.label}</strong>
        </button>
    );
    const renderProtocolButton = (
        nextMode: PreviewMode,
        icon: ToolbarIconName,
        disabled: boolean,
        disabledTitle?: string,
    ) => {
        const label = previewModeLabels[nextMode];
        return (
            <button
                type="button"
                className={mode === nextMode ? 'active' : ''}
                disabled={disabled}
                aria-label={label}
                title={disabledTitle ? `${label}：${disabledTitle}` : label}
                onClick={() => onModeChange(nextMode)}
            >
                <ToolbarIcon name={icon} />
            </button>
        );
    };

    return (
        <div className="preview-toolbar">
            <div className="preview-toolbar-group stream-toolbar-group">
                <div className="stream-switcher">
                    {renderStreamButton('main', mainSummary)}
                    {renderStreamButton('sub', subSummary)}
                </div>
            </div>
            <div className="preview-toolbar-group protocol-toolbar-group">
                <span className="preview-protocol-label">拉流协议</span>
                <div className="preview-actions protocol-actions">
                    {renderProtocolButton(
                        'webrtc',
                        'webrtc',
                        !webrtcEnabled ||
                            !webrtcSupported ||
                            !webrtcPreviewEnabled,
                        protocolDisabledText(
                            webrtcSupported,
                            webrtcPreviewEnabled,
                            '当前编码或浏览器不支持 WebRTC 预览',
                        ),
                    )}
                    {renderProtocolButton(
                        'hls',
                        'hls',
                        !hlsSupported || !hlsModeEnabled,
                        protocolDisabledText(
                            hlsSupported,
                            hlsModeEnabled,
                            '当前编码或浏览器不支持 HLS 预览',
                        ),
                    )}
                    {renderProtocolButton(
                        'flv',
                        'flv',
                        !flvSupported || !flvPreviewEnabled,
                        protocolDisabledText(
                            flvSupported,
                            flvPreviewEnabled,
                            '当前编码不支持 HTTP-FLV 预览',
                        ),
                    )}
                    {renderProtocolButton(
                        'mjpeg',
                        'mjpeg',
                        !mjpegSupported || !mjpegPreviewEnabled,
                        protocolDisabledText(
                            mjpegSupported,
                            mjpegPreviewEnabled,
                            '当前编码不支持 MJPEG 预览',
                        ),
                    )}
                </div>
            </div>
            <div className="preview-toolbar-right">
                <div className="preview-actions preview-command-actions">
                    {onSnapshot && (
                        <button
                            type="button"
                            aria-label="抓图"
                            disabled={!streamRunning}
                            title={
                                streamRunning
                                    ? '打开当前码流抓图'
                                    : '码流未运行，无法抓图'
                            }
                            onClick={() => onSnapshot(stream)}
                        >
                            <ToolbarIcon name="camera" />
                        </button>
                    )}
                    <button
                        type="button"
                        aria-label="全屏"
                        title="进入或退出全屏预览"
                        onClick={onToggleFullscreen}
                    >
                        <ToolbarIcon name="fullscreen" />
                    </button>
                </div>
            </div>
        </div>
    );
}
