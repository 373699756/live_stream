import type { StreamName } from '../api/types';
import { previewModeLabels, type PreviewMode } from '../hooks/previewMode';
import type { PreviewStreamSummary } from './previewDisplay';

interface PreviewToolbarProps {
  flvPlaybackEnabled: boolean;
  flvSupported: boolean;
  hlsPlaybackEnabled: boolean;
  hlsSupported: boolean;
  mainSummary: PreviewStreamSummary;
  mjpegPlaybackEnabled: boolean;
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
  webrtcPlaybackEnabled: boolean;
  webrtcSupported: boolean;
}

export function PreviewToolbar({
  flvPlaybackEnabled,
  flvSupported,
  hlsPlaybackEnabled,
  hlsSupported,
  mainSummary,
  mjpegPlaybackEnabled,
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
  webrtcPlaybackEnabled,
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
      <span className={summary.running ? 'running' : ''}>{summary.state}</span>
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
          disabled={!webrtcEnabled || !webrtcSupported || !webrtcPlaybackEnabled}
          title={!webrtcSupported ? '当前编码不支持 WebRTC 预览' : undefined}
          onClick={() => onModeChange('webrtc')}
        >
          {previewModeLabels.webrtc}
        </button>
        <button
          type="button"
          className={mode === 'hls' ? 'active' : ''}
          disabled={!hlsSupported || !hlsPlaybackEnabled}
          title={!hlsSupported ? '当前编码不支持 HLS 预览' : undefined}
          onClick={() => onModeChange('hls')}
        >
          {previewModeLabels.hls}
        </button>
        <button
          type="button"
          className={mode === 'flv' ? 'active' : ''}
          disabled={!flvSupported || !flvPlaybackEnabled}
          title={!flvSupported ? '当前编码不支持 HTTP-FLV 预览' : undefined}
          onClick={() => onModeChange('flv')}
        >
          {previewModeLabels.flv}
        </button>
        <button
          type="button"
          className={mode === 'mjpeg' ? 'active' : ''}
          disabled={!mjpegSupported || !mjpegPlaybackEnabled}
          title={!mjpegSupported ? '当前编码不支持 MJPEG 预览' : undefined}
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
        <button type="button" onClick={onToggleFullscreen}>全屏</button>
      </div>
    </div>
  );
}
