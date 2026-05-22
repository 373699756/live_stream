import { useRef, useState } from 'react';
import type { StreamName, StreamStatus } from '../api/types';
import {
  previewModeLabels,
  usePreviewPlayer,
  type PreviewMode,
} from '../hooks/usePreviewPlayer';
import { StatusBadge } from './StatusBadge';

interface VideoPreviewProps {
  stream: StreamName;
  statuses: StreamStatus[];
  onStreamChange: (stream: StreamName) => void;
  enabled?: boolean;
  onSnapshot?: (stream: StreamName) => void;
}

export function VideoPreview({
  stream,
  statuses,
  onStreamChange,
  enabled = true,
  onSnapshot,
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
    hlsSupported,
    previewState,
    restartPreview,
    streamRunning,
    switchMode,
    videoRef,
    webrtcEnabled,
    webrtcPlaybackEnabled,
    webrtcSupported,
  } = usePreviewPlayer({ active, enabled, mode, setMode, stream });
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

  const streamLabel = stream === 'main' ? '主码流' : '子码流';
  const streamText = (value: string | number | undefined, fallback = '未知') =>
    value === undefined || value === '' ? fallback : String(value);
  const previewDetail =
    mode === 'webrtc'
      ? `${streamLabel} / WebRTC`
      : mode === 'hls'
        ? `${streamLabel} / HLS`
        : mode === 'flv'
          ? `${streamLabel} / HTTP-FLV`
          : `${streamLabel} / WebRTC`;
  const protocolLabel = previewModeLabels[mode];
  const streamSummary = (name: StreamName) => {
    const item = statuses.find((status) => status.stream === name);
    const running = item?.state === 'running';
    return {
      label: name === 'main' ? '主码流' : '子码流',
      running,
      state: running ? '运行中' : '未运行',
      detail: `${streamText(item?.codec)} / ${streamText(item?.resolution, '--')} / ${streamText(item?.fps, '--')}fps`,
    };
  };
  const mainSummary = streamSummary('main');
  const subSummary = streamSummary('sub');

  return (
    <section className="preview-panel">
      <div className="preview-toolbar">
        <div className="stream-switcher">
          <button
            type="button"
            className={stream === 'main' ? 'active' : ''}
            onClick={() => switchStream('main')}
          >
            <strong>{mainSummary.label}</strong>
            <span className={mainSummary.running ? 'running' : ''}>{mainSummary.state}</span>
            <em>{mainSummary.detail}</em>
          </button>
          <button
            type="button"
            className={stream === 'sub' ? 'active' : ''}
            onClick={() => switchStream('sub')}
          >
            <strong>{subSummary.label}</strong>
            <span className={subSummary.running ? 'running' : ''}>{subSummary.state}</span>
            <em>{subSummary.detail}</em>
          </button>
        </div>
        <div className="preview-actions">
          <button
            type="button"
            className={mode === 'webrtc' ? 'active' : ''}
            disabled={!webrtcEnabled || !webrtcSupported || !webrtcPlaybackEnabled}
            title={!webrtcSupported ? '当前编码不支持 WebRTC 预览' : undefined}
            onClick={() => switchMode('webrtc')}
          >
            WebRTC
          </button>
          <button
            type="button"
            className={mode === 'hls' ? 'active' : ''}
            disabled={!hlsSupported || !streamRunning}
            title={!hlsSupported ? '当前编码不支持 HLS 预览' : undefined}
            onClick={() => switchMode('hls')}
          >
            HLS
          </button>
          <button
            type="button"
            className={mode === 'flv' ? 'active' : ''}
            disabled={!flvSupported || !flvPlaybackEnabled}
            title={!flvSupported ? '当前编码不支持 HTTP-FLV 预览' : undefined}
            onClick={() => switchMode('flv')}
          >
            HTTP-FLV
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
          <button type="button" onClick={toggleFullscreen}>全屏</button>
        </div>
      </div>

      <div className="video-surface" ref={surfaceRef} onDoubleClick={toggleFullscreen}>
        {!enabled ? (
          <div className="video-placeholder">
            <div className="lens-ring paused" />
            <strong>预览已暂停</strong>
            <span>正在应用视频参数</span>
          </div>
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
      </div>

      <div className="preview-footer">
        <StatusBadge state={active?.state === 'running' ? 'running' : 'pending'} />
        <span>{streamLabel}</span>
        <span>{protocolLabel}</span>
        <span>{streamText(active?.codec)}</span>
        <span>分辨率 {streamText(active?.resolution, '--')}</span>
        {decodedSize && <span>实际 {decodedSize}</span>}
        {displaySize && <span>显示 {displaySize}</span>}
        <span>{streamText(active?.fps, '--')} fps</span>
        <span>{streamText(active?.bitrateKbps, '--')} kbps</span>
      </div>
    </section>
  );
}
