import { useEffect, useRef, useState } from 'react';
import type { StreamName, StreamStatus } from '../api/types';
import { StatusBadge } from './StatusBadge';

interface VideoPreviewProps {
  stream: StreamName;
  statuses: StreamStatus[];
  onStreamChange: (stream: StreamName) => void;
}

export function VideoPreview({ stream, statuses, onStreamChange }: VideoPreviewProps) {
  const [mode, setMode] = useState<'webrtc' | 'snapshot'>('snapshot');
  const [snapshotTick, setSnapshotTick] = useState(0);
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const active = statuses.find((item) => item.stream === stream);
  const snapshotUrl = `/api/snapshot/${stream}.jpg?t=${snapshotTick}`;

  useEffect(() => {
    if (mode !== 'snapshot') {
      return;
    }
    const timer = window.setInterval(() => setSnapshotTick((value) => value + 1), 2000);
    return () => window.clearInterval(timer);
  }, [mode]);

  const openSnapshot = () => {
    window.open(`/api/snapshot/${stream}.jpg`, '_blank', 'noopener,noreferrer');
  };

  const requestFullscreen = () => {
    void surfaceRef.current?.requestFullscreen?.();
  };

  return (
    <section className="preview-panel">
      <div className="preview-toolbar">
        <div className="segmented">
          <button
            type="button"
            className={stream === 'main' ? 'active' : ''}
            onClick={() => onStreamChange('main')}
          >
            主码流
          </button>
          <button
            type="button"
            className={stream === 'sub' ? 'active' : ''}
            onClick={() => onStreamChange('sub')}
          >
            子码流
          </button>
        </div>
        <div className="preview-actions">
          <button
            type="button"
            className={mode === 'webrtc' ? 'active' : ''}
            onClick={() => setMode('webrtc')}
          >
            WebRTC
          </button>
          <button
            type="button"
            className={mode === 'snapshot' ? 'active' : ''}
            onClick={() => setMode('snapshot')}
          >
            抓图预览
          </button>
          <button type="button" onClick={openSnapshot}>截图</button>
          <button type="button" onClick={requestFullscreen}>全屏</button>
        </div>
      </div>

      <div className="video-surface" ref={surfaceRef}>
        {mode === 'webrtc' ? (
          <video ref={videoRef} className="video-element" autoPlay muted playsInline />
        ) : (
          <img
            className="snapshot-preview"
            src={snapshotUrl}
            alt="snapshot preview"
            onLoad={(event) => {
              event.currentTarget.style.opacity = '1';
            }}
            onError={(event) => {
              event.currentTarget.style.opacity = '0';
            }}
          />
        )}
        <div className="video-placeholder">
          <div className="lens-ring" />
          <strong>{mode === 'webrtc' ? '等待 WebRTC 视频流' : '抓图预览'}</strong>
          <span>后端未接入时显示模拟预览状态</span>
        </div>
      </div>

      <div className="preview-footer">
        <StatusBadge state={active?.state === 'running' ? 'running' : 'pending'} />
        <span>{active?.codec || 'H.264'}</span>
        <span>分辨率 {active?.resolution || '1920x1080'}</span>
        <span>{active?.fps || 25} fps</span>
        <span>{active?.bitrateKbps || 4096} kbps</span>
      </div>
    </section>
  );
}
